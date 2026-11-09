package application

import (
	"context"
	"crypto/tls"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/http/httputil"
	"net/url"
	"sync"
	"testing"
	"time"
)

// Real TLS termination on both client-facing connections, plaintext on the
// isolated backend connections. Ephemeral frontend ports keep tests independent
// of an installed server; advertised product ports are checked before remapping.
func proxyFrontends(t *testing.T, addresses map[string]string) *tls.Config {
	t.Helper()
	if addresses["tls"] != "https://game.example.org:18443" || addresses["lobby"] != "game.example.org:15200" {
		t.Fatal("proxy published upstream endpoints", addresses)
	}
	upstream, err := url.Parse(addresses["proxy_http"])
	if err != nil {
		t.Fatal(err)
	}
	httpProxy := httptest.NewTLSServer(httputil.NewSingleHostReverseProxy(upstream))
	t.Cleanup(httpProxy.Close)
	config := httpProxy.Client().Transport.(*http.Transport).TLSClientConfig.Clone()
	lobbyProxy, err := tls.Listen("tcp4", "127.0.0.1:0", &tls.Config{Certificates: httpProxy.TLS.Certificates, MinVersion: tls.VersionTLS12})
	if err != nil {
		t.Fatal(err)
	}
	var workers sync.WaitGroup
	var mutex sync.Mutex
	var connections []net.Conn
	lobbyUpstream := addresses["proxy_lobby"]
	workers.Add(1)
	go func() {
		defer workers.Done()
		for {
			client, err := lobbyProxy.Accept()
			if err != nil {
				return
			}
			mutex.Lock()
			connections = append(connections, client)
			mutex.Unlock()
			workers.Add(1)
			go func() {
				defer workers.Done()
				defer client.Close()
				backend, err := net.DialTimeout("tcp4", lobbyUpstream, time.Second)
				if err != nil {
					return
				}
				defer backend.Close()
				client.SetDeadline(time.Now().Add(30 * time.Second))
				backend.SetDeadline(time.Now().Add(30 * time.Second))
				copied := make(chan struct{})
				go func() { io.Copy(backend, client); backend.Close(); close(copied) }()
				io.Copy(client, backend)
				client.Close()
				<-copied
			}()
		}
	}()
	t.Cleanup(func() {
		lobbyProxy.Close()
		mutex.Lock()
		for _, connection := range connections {
			connection.Close()
		}
		mutex.Unlock()
		workers.Wait()
	})
	// Discovery still advertises the external HTTPS address behind the proxy.
	response, err := httpProxy.Client().Get(httpProxy.URL + "/locate?service=auth")
	if err != nil {
		t.Fatal(err)
	}
	body, err := io.ReadAll(response.Body)
	response.Body.Close()
	if err != nil || string(body) != "game.example.org:18443" {
		t.Fatal("wrong proxied discovery", string(body), err)
	}
	addresses["tls"] = httpProxy.URL
	addresses["lobby"] = lobbyProxy.Addr().String()
	return config
}

func TestProxyRejectsPublicUpstreamsAndMissingDomain(t *testing.T) {
	for _, mode := range []string{"missing-domain", "http", "lobby"} {
		t.Run(mode, func(t *testing.T) {
			db, adapter, options, _ := setup(t)
			options.ProxyTLS = true
			options.Domain = "game.example.org"
			switch mode {
			case "missing-domain":
				options.Domain = ""
			case "http":
				options.TLS = "0.0.0.0:0"
			case "lobby":
				options.Lobby = "0.0.0.0:0"
			}
			if err := RunNative(context.Background(), db, adapter, options); err == nil {
				t.Fatal("unsafe proxy configuration accepted")
			}
		})
	}
}
