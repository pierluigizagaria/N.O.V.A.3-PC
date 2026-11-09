package native

import (
	"context"
	"crypto/tls"
	"encoding/binary"
	"errors"
	"io"
	"net"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/protocol/pc"
)

// ServeProtectedLobby accepts the updated PC transport: verified TLS followed
// by NV3L/version1, the opaque 64-byte login credential and capability bits.
// Snapshot-v1 is required; there is no silent downgrade. The native lobby
// frames that follow remain unchanged. No plaintext fallback is accepted here.
// Normal startup pairs this listener with the selected-server PC adapter.
func (s *Services) ServeProtectedLobby(ctx context.Context, listener net.Listener) error {
	return s.serveCredentialLobby(ctx, listener, false)
}

func loopbackPeer(address string) bool {
	host, _, err := net.SplitHostPort(address)
	return err == nil && net.ParseIP(host).IsLoopback()
}

// ServeProxyLobby accepts the same authenticated preface after a same-host
// proxy terminates TLS. It cannot be bound to a public or LAN interface.
func (s *Services) ServeProxyLobby(ctx context.Context, listener net.Listener) error {
	if !loopbackPeer(listener.Addr().String()) {
		return errors.New("reverse proxy lobby requires a loopback listener")
	}
	return s.serveCredentialLobby(ctx, listener, true)
}

func (s *Services) serveCredentialLobby(ctx context.Context, listener net.Listener, proxy bool) error {
	return s.serveLobby(ctx, listener, func(connection net.Conn) {
		defer connection.Close()
		if err := connection.SetDeadline(time.Now().Add(5 * time.Second)); err != nil {
			return
		}
		if proxy {
			if !loopbackPeer(connection.RemoteAddr().String()) {
				return
			}
		} else {
			secure, ok := connection.(*tls.Conn)
			if !ok {
				return
			}
			if err := secure.HandshakeContext(ctx); err != nil {
				return
			}
		}
		s.credentialLobbyConnection(connection)
	})
}

// credentialLobbyConnection is entered only after TLS or explicit loopback proxy admission.
func (s *Services) credentialLobbyConnection(connection net.Conn) {
	var preface [73]byte
	if _, err := io.ReadFull(connection, preface[:]); err != nil || string(preface[:5]) != "NV3L\x01" {
		return
	}
	capabilities := binary.BigEndian.Uint32(preface[69:])
	if capabilities&pc.SnapshotV1 == 0 || capabilities & ^uint32(pc.SnapshotV1|socialCapability|socialEventCapability) != 0 ||
		(capabilities&socialEventCapability != 0 && capabilities&socialCapability == 0) {
		return
	}
	credential := string(preface[5:69])
	clear(preface[:])
	account, err := accounts.Resolve(s.Store, credential)
	if err != nil {
		return
	}
	if err = s.Sessions.Login(account, time.Now(), time.Hour); err != nil {
		return
	}
	if err = connection.SetDeadline(time.Time{}); err != nil {
		return
	}
	s.lobbyConnectionWithCapabilities(connection, account, credential, capabilities)
}
