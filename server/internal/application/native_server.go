// Package application composes local services without putting their lifecycle in the CLI.
package application

import (
	"context"
	"crypto/tls"
	"encoding/json"
	"errors"
	"io"
	"log"
	"net"
	"net/http"
	"net/netip"
	"sync"
	"time"

	"nova3.test/backend/internal/adapters/shop"
	"nova3.test/backend/internal/authority"
	"nova3.test/backend/internal/buildmode"
	"nova3.test/backend/internal/native"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/transport/community"
	"nova3.test/backend/internal/transport/loopback"
)

type NativeServerOptions struct {
	Authority     *authority.Definitions
	HTTP          string
	Eve           string
	TLS           string
	LegacyTLS     string
	Lobby         string
	Compatibility string
	Directory     string
	Ready         io.Writer
	Diagnostics   io.Writer
	Domain        string
	Address       string
	ProxyTLS      bool // TLS terminates at a same-host reverse proxy; upstreams are loopback only.
	SharedPort    bool // Installed service: HTTPS and credential lobby share one listener.
	Match         string
	ResolveIPv4   func(context.Context, string) ([]netip.Addr, error)
}

func RunNative(ctx context.Context, db *persistence.Store, catalogue *shop.Adapter, options NativeServerOptions) (result error) {
	if err := ctx.Err(); err != nil {
		return err
	}
	if options.Ready == nil || options.Diagnostics == nil {
		return errors.New("native service output writers are required")
	}
	if options.Address != "" && (!community.ValidAddress(options.Address) || options.Domain != "") {
		return errors.New("address must be one unicast IPv4 address, separate from domain")
	}
	if options.ProxyTLS && (!community.ValidDomain(options.Domain) || options.Address != "") {
		return errors.New("reverse proxy mode requires a domain and no address override")
	}
	external := options.Domain != "" || options.Address != ""
	runContext, cancelRun := context.WithCancel(ctx)
	defer cancelRun()
	var certificate tls.Certificate
	var err error
	if options.ProxyTLS {
		// The proxy owns certificates, renewal and public TLS listeners.
	} else if options.Domain != "" {
		certificate, err = community.Identity(options.Directory, options.Domain, time.Now())
	} else {
		certificate, err = loopback.Certificate(options.Directory)
	}
	if err != nil {
		return err
	}
	type binding struct {
		name, address string
		secure, lobby bool
	}
	bindings := []binding{{"http", options.HTTP, false, false}, {"eve", options.Eve, false, false}, {"tls", options.TLS, true, false}, {"legacy_tls", options.LegacyTLS, true, false}, {"lobby", options.Lobby, true, true}}
	if external {
		// Updated PC bootstrap and shop requests share the HTTPS listener.
		// Public deployment needs neither plaintext nor privileged-port binds.
		bindings = []binding{{"tls", options.TLS, true, false}, {"lobby", options.Lobby, true, true}}
	}
	if options.ProxyTLS {
		bindings = []binding{{"tls", options.TLS, false, false}, {"lobby", options.Lobby, false, true}}
	}
	if buildmode.Developer && !external {
		bindings = append(bindings, binding{"compatibility", options.Compatibility, false, false})
	}
	if options.SharedPort {
		bindings = []binding{{"tls", options.TLS, !options.ProxyTLS, false}}
	}
	listeners := make(map[string]net.Listener)
	defer func() {
		for _, listener := range listeners {
			listener.Close()
		}
	}()
	for _, binding := range bindings {
		var listener net.Listener
		var listenErr error
		if external && !options.ProxyTLS && (binding.name == "tls" || binding.lobby) {
			listener, listenErr = net.Listen("tcp4", binding.address)
		} else {
			listener, listenErr = loopback.Listen(binding.address)
		}
		if listenErr != nil {
			return listenErr
		}
		listeners[binding.name] = listener
	}
	lobbyListener := listeners["lobby"]
	if options.SharedPort {
		lobbyListener = listeners["tls"]
	}
	endpoints := native.Endpoints{TLS: "https://" + listeners["tls"].Addr().String(), Lobby: lobbyListener.Addr().String()}
	if external {
		_, tlsPort, _ := net.SplitHostPort(listeners["tls"].Addr().String())
		_, lobbyPort, _ := net.SplitHostPort(lobbyListener.Addr().String())
		if options.ProxyTLS {
			tlsPort, lobbyPort = "18443", "15200"
			if options.SharedPort {
				lobbyPort = tlsPort
			}
		}
		host := options.Domain
		if host == "" {
			host = options.Address
		}
		endpoints.TLS = "https://" + net.JoinHostPort(host, tlsPort)
		endpoints.HTTP = endpoints.TLS
		endpoints.Lobby = net.JoinHostPort(host, lobbyPort)
	} else if options.SharedPort {
		endpoints.HTTP = endpoints.TLS
	} else {
		endpoints.HTTP = "http://" + listeners["http"].Addr().String()
	}
	service := &native.Services{Store: db, Shop: catalogue, Authority: options.Authority, Endpoints: endpoints, Log: log.New(options.Diagnostics, "", log.LstdFlags|log.LUTC)}
	service.ProxyTLS = options.ProxyTLS
	matchAddress := options.Match
	if matchAddress == "" {
		matchAddress = "127.0.0.1:0"
	}
	udpAddress, err := net.ResolveUDPAddr("udp4", matchAddress)
	if err != nil {
		return err
	}
	udp, err := net.ListenUDP("udp4", udpAddress)
	if err != nil {
		return err
	}
	defer udp.Close()
	var advertised netip.Addr
	if options.Address != "" {
		advertised = netip.MustParseAddr(options.Address)
	}
	if options.Domain != "" {
		lookup, cancel := context.WithTimeout(runContext, 5*time.Second)
		resolve := options.ResolveIPv4
		if resolve == nil {
			resolve = func(ctx context.Context, host string) ([]netip.Addr, error) {
				return net.DefaultResolver.LookupNetIP(ctx, "ip4", host)
			}
		}
		addresses, lookupErr := resolve(lookup, options.Domain)
		cancel()
		if lookupErr != nil {
			return lookupErr
		}
		if len(addresses) == 0 {
			return errors.New("server domain needs an IPv4 address for game transport")
		}
		advertised = addresses[0].Unmap()
	}
	service.PrepareMatches(udp, advertised)
	server := &http.Server{Handler: service.Handler(), ReadHeaderTimeout: 3 * time.Second, ReadTimeout: 5 * time.Second, WriteTimeout: 5 * time.Second, IdleTimeout: 10 * time.Second, MaxHeaderBytes: 8192, ErrorLog: service.Log,
		BaseContext: func(net.Listener) context.Context { return runContext },
	}
	errorsChannel := make(chan error, len(bindings)+1)
	var serving sync.WaitGroup
	defer func() {
		cancelRun()
		service.Sessions.Close()
		// Stop admission first, including listeners whose Serve goroutine has
		// not yet been scheduled. This also wakes the lobby's Accept loop.
		for _, listener := range listeners {
			listener.Close()
		}
		shutdown, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		if err := server.Shutdown(shutdown); err != nil {
			server.Close()
			// Admission listeners were deliberately closed above. Depending on
			// Serve's timing, Shutdown can observe that same completed close.
			if !errors.Is(err, net.ErrClosed) {
				result = errors.Join(result, err)
			}
		}
		serving.Wait() // The lobby waits for every accepted stream handler.
	}()
	for _, binding := range bindings {
		listener := listeners[binding.name]
		serving.Add(1)
		if binding.secure {
			listener = tls.NewListener(listener, loopback.ModernTLS(certificate))
		}
		if options.SharedPort {
			go func() {
				defer serving.Done()
				errorsChannel <- service.ServeShared(runContext, listener, server, options.ProxyTLS)
			}()
			continue
		}
		if binding.lobby {
			go func() {
				defer serving.Done()
				if options.ProxyTLS {
					errorsChannel <- service.ServeProxyLobby(runContext, listener)
				} else {
					errorsChannel <- service.ServeProtectedLobby(runContext, listener)
				}
			}()
			continue
		}
		go func() { defer serving.Done(); errorsChannel <- server.Serve(listener) }()
	}
	serving.Add(1)
	go func() { defer serving.Done(); errorsChannel <- service.ServeMatches(runContext, udp) }()
	ready := map[string]string{"tls": endpoints.TLS, "lobby": endpoints.Lobby, "version": catalogue.Catalogue.Version}
	ready["match"] = udp.LocalAddr().String()
	ready["match_status"] = "native competitive modes, animated combat and room invitations; gameplay parity and mounted physics remain under verification"
	if options.ProxyTLS {
		ready["proxy_http"] = "http://" + listeners["tls"].Addr().String()
		ready["proxy_lobby"] = lobbyListener.Addr().String()
	}
	if !external && !options.SharedPort {
		ready["http"] = endpoints.HTTP
		ready["eve"] = "http://" + listeners["eve"].Addr().String()
		ready["legacy_tls"] = "https://" + listeners["legacy_tls"].Addr().String()
	}
	if buildmode.Developer && !external && !options.SharedPort {
		ready["compatibility"] = "http://" + listeners["compatibility"].Addr().String()
	}
	if err = json.NewEncoder(options.Ready).Encode(ready); err != nil {
		return err
	}
	select {
	case <-ctx.Done():
		return nil
	case err = <-errorsChannel:
		if ctx.Err() != nil && (errors.Is(err, http.ErrServerClosed) || errors.Is(err, net.ErrClosed) || errors.Is(err, context.Canceled)) {
			return nil
		}
		return err
	}
}
