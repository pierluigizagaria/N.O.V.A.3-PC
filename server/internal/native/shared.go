package native

import (
	"bufio"
	"context"
	"crypto/tls"
	"errors"
	"net"
	"net/http"
	"sync"
	"time"
)

type sharedConnection struct {
	net.Conn
	reader   *bufio.Reader
	tlsState *tls.ConnectionState
}

func (c *sharedConnection) Read(p []byte) (int, error) { return c.reader.Read(p) }

// sharedHTTPListener transfers already admitted HTTP streams without opening
// another socket. Closing it also releases dispatchers waiting for Accept.
type sharedHTTPListener struct {
	net.Listener
	streams chan net.Conn
	done    chan struct{}
	once    sync.Once
}

func (l *sharedHTTPListener) Accept() (net.Conn, error) {
	select {
	case c := <-l.streams:
		return c, nil
	case <-l.done:
		return nil, net.ErrClosed
	}
}

func (l *sharedHTTPListener) Close() error {
	l.once.Do(func() { close(l.done) })
	return nil
}

type sharedTLSContextKey struct{}

// ServeShared admits TLS once, then routes the authenticated lobby preface or
// HTTP to the existing consumers. Proxy plaintext is restricted to loopback.
func (s *Services) ServeShared(ctx context.Context, listener net.Listener, server *http.Server, proxy bool) error {
	if proxy && !loopbackPeer(listener.Addr().String()) {
		return errors.New("shared reverse proxy upstream requires loopback")
	}
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	httpListener := &sharedHTTPListener{Listener: listener, streams: make(chan net.Conn), done: make(chan struct{})}
	// net/http recognizes only concrete *tls.Conn values. Preserve the verified
	// TLS state when replaying the bytes inspected for protocol routing.
	originalHandler, originalContext := server.Handler, server.ConnContext
	server.ConnContext = func(ctx context.Context, c net.Conn) context.Context {
		if originalContext != nil {
			ctx = originalContext(ctx, c)
		}
		if shared, ok := c.(*sharedConnection); ok && shared.tlsState != nil {
			ctx = context.WithValue(ctx, sharedTLSContextKey{}, shared.tlsState)
		}
		return ctx
	}
	server.Handler = http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if state, ok := r.Context().Value(sharedTLSContextKey{}).(*tls.ConnectionState); ok {
			r.TLS = state
		}
		originalHandler.ServeHTTP(w, r)
	})
	results := make(chan error, 2)
	var serving sync.WaitGroup
	serving.Add(2)
	go func() { defer serving.Done(); results <- server.Serve(httpListener) }()
	go func() {
		defer serving.Done()
		results <- s.serveLobby(ctx, listener, func(connection net.Conn) {
			handedOff := false
			defer func() {
				if !handedOff {
					connection.Close()
				}
			}()
			if connection.SetDeadline(time.Now().Add(5*time.Second)) != nil {
				return
			}
			var state *tls.ConnectionState
			if proxy {
				if !loopbackPeer(connection.RemoteAddr().String()) {
					return
				}
			} else {
				secure, ok := connection.(*tls.Conn)
				if !ok || secure.HandshakeContext(ctx) != nil {
					return
				}
				verified := secure.ConnectionState()
				state = &verified
			}
			reader := bufio.NewReaderSize(connection, 1024)
			prefix, err := reader.Peek(5)
			if err != nil {
				return
			}
			stream := &sharedConnection{Conn: connection, reader: reader, tlsState: state}
			if string(prefix) == "NV3L\x01" {
				s.credentialLobbyConnection(stream)
				return
			}
			if connection.SetDeadline(time.Time{}) != nil {
				return
			}
			select {
			case httpListener.streams <- stream:
				handedOff = true
			case <-httpListener.done:
			case <-ctx.Done():
			}
		})
	}()
	var err error
	select {
	case err = <-results:
	case <-ctx.Done():
		err = ctx.Err()
	}
	cancel()
	listener.Close()
	httpListener.Close()
	serving.Wait()
	return err
}
