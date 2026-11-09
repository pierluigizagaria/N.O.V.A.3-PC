package application

import (
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"encoding/binary"
	"encoding/json"
	"encoding/pem"
	"errors"
	"io"
	"math/big"
	"net"
	"net/http"
	"net/netip"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/adapters/shop"
	"nova3.test/backend/internal/authority"
	"nova3.test/backend/internal/buildmode"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/protocol/legacy"
	"nova3.test/backend/internal/protocol/lobby"
	"nova3.test/backend/internal/testfixture"
	"nova3.test/backend/internal/transport/loopback"
)

type readiness struct {
	addresses chan map[string]string
	fail      bool
}

func (r readiness) Write(data []byte) (int, error) {
	var addresses map[string]string
	if err := json.Unmarshal(data, &addresses); err != nil {
		return 0, err
	}
	r.addresses <- addresses
	if r.fail {
		return 0, errors.New("readiness unavailable")
	}
	return len(data), nil
}

func setup(t *testing.T) (*persistence.Store, *shop.Adapter, NativeServerOptions, []string) {
	t.Helper()
	directory := t.TempDir()
	db, err := persistence.Open(filepath.Join(directory, "test.db"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { db.Close() })
	catalogue, mapping := testfixture.NativeCatalogue()
	if err = db.Admit(catalogue); err != nil {
		t.Fatal(err)
	}
	adapter, err := shop.New(catalogue, mapping)
	if err != nil {
		t.Fatal(err)
	}
	var ids []string
	for _, name := range []string{"alice", "bob"} {
		account, err := accounts.Create(db, name)
		if err != nil {
			t.Fatal(err)
		}
		if err = grants.Provision(db, account.ID, catalogue.Version); err != nil {
			t.Fatal(err)
		}
		if err = accounts.SetPassword(db, account.ID, "test-password"); err != nil {
			t.Fatal(err)
		}
		ids = append(ids, account.ID)
	}
	definitions, err := authority.Decode(testfixture.NativeAuthority())
	if err != nil {
		t.Fatal(err)
	}
	return db, adapter, NativeServerOptions{Authority: definitions, HTTP: "127.0.0.1:0", Eve: "127.0.0.1:0", TLS: "127.0.0.1:0", LegacyTLS: "127.0.0.1:0", Lobby: "127.0.0.1:0", Compatibility: "127.0.0.1:0", Directory: directory, Ready: io.Discard, Diagnostics: io.Discard}, ids
}

func ready(t *testing.T, channel <-chan map[string]string) map[string]string {
	t.Helper()
	select {
	case value := <-channel:
		return value
	case <-time.After(5 * time.Second):
		t.Fatal("service not ready")
		return nil
	}
}

func stopped(t *testing.T, done <-chan error, wantError bool) {
	t.Helper()
	select {
	case err := <-done:
		if (err != nil) != wantError {
			t.Fatalf("shutdown error=%v expectedError=%t", err, wantError)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("service did not drain")
	}
}

func available(t *testing.T, addresses map[string]string) {
	t.Helper()
	for name, address := range addresses {
		if name == "version" || name == "match_status" {
			continue
		}
		if name == "match" {
			addr, err := net.ResolveUDPAddr("udp4", address)
			if err != nil {
				t.Fatal(err)
			}
			conn, err := net.ListenUDP("udp4", addr)
			if err != nil {
				t.Fatal("match listener not released", err)
			}
			conn.Close()
			continue
		}
		address = strings.TrimPrefix(strings.TrimPrefix(address, "https://"), "http://")
		listener, err := net.Listen("tcp4", address)
		if err != nil {
			t.Fatalf("listener %s not released: %v", name, err)
		}
		listener.Close()
	}
}

func legacyRequest(t *testing.T, base, fields string) string {
	t.Helper()
	client := &http.Client{Timeout: 3 * time.Second}
	response, err := client.Get(base + "/legacy?b=" + legacy.Encode([]byte(fields)))
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	body, err := io.ReadAll(response.Body)
	if err != nil || response.StatusCode != 200 {
		t.Fatalf("legacy response status=%d err=%v", response.StatusCode, err)
	}
	return string(body)
}

func connect(t *testing.T, endpoint string) net.Conn {
	t.Helper()
	connection, err := net.DialTimeout("tcp4", endpoint, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { connection.Close() })
	connection.SetDeadline(time.Now().Add(3 * time.Second))
	return connection
}

func trustedTransport(t *testing.T, directory string) *tls.Config {
	t.Helper()
	pem, err := os.ReadFile(filepath.Join(directory, "localhost.pem"))
	if err != nil {
		t.Fatal(err)
	}
	roots := x509.NewCertPool()
	if !roots.AppendCertsFromPEM(pem) {
		t.Fatal("generated certificate is invalid")
	}
	return &tls.Config{RootCAs: roots, MinVersion: tls.VersionTLS12}
}

func passwordCredential(t *testing.T, config *tls.Config, base, name string) string {
	t.Helper()
	transport := &http.Transport{TLSClientConfig: config}
	defer transport.CloseIdleConnections()
	client := &http.Client{Transport: transport, Timeout: 3 * time.Second}
	response, err := client.Get(base + "/legacy?b=" + legacy.Encode([]byte("f|15|u|"+name+"|p|test-password|")))
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	body, err := io.ReadAll(response.Body)
	token := response.Header.Get("X-Nova-Session")
	if err != nil || response.StatusCode != http.StatusOK || !strings.Contains(string(body), "|r|s|") || len(token) != 64 {
		t.Fatal("protected password login rejected")
	}
	return token
}

func protectedConnect(t *testing.T, config *tls.Config, endpoint, token string) net.Conn {
	t.Helper()
	connection, err := tls.DialWithDialer(&net.Dialer{Timeout: time.Second}, "tcp4", endpoint, config)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { connection.Close() })
	connection.SetDeadline(time.Now().Add(3 * time.Second))
	preface := append([]byte("NV3L\x01"), []byte(token)...)
	preface = append(preface, 0, 0, 0, 1)
	if _, err = connection.Write(preface); err != nil {
		t.Fatal(err)
	}
	return connection
}

func login(t *testing.T, connection net.Conn, id string, accepted bool) {
	t.Helper()
	if err := lobby.Write(connection, lobby.Frame{Opcode: lobby.EstablishRequest, Payload: make([]byte, 4)}); err != nil {
		t.Fatal(err)
	}
	frame, err := lobby.Read(connection)
	if err != nil || frame.Opcode != lobby.Established {
		t.Fatal(frame, err)
	}
	payload := make([]byte, 4)
	payload = append(payload, lobby.String(id)...)
	payload = append(payload, 0, 0, 0, 0, 1)
	if err = lobby.Write(connection, lobby.Frame{Opcode: lobby.LoginRequest, Payload: payload}); err != nil {
		t.Fatal(err)
	}
	frame, err = lobby.Read(connection)
	if accepted && (err != nil || frame.Opcode != lobby.LoginResponse || len(frame.Payload) < 4 || binary.BigEndian.Uint32(frame.Payload[:4]) != 0) {
		t.Fatal("login rejected", frame, err)
	}
	if !accepted && err == nil {
		t.Fatal("duplicate account accepted")
	}
}

func TestNativeLifecycleTwoAccountsAndDuplicateAdmission(t *testing.T) {
	db, adapter, options, ids := setup(t)
	output := readiness{addresses: make(chan map[string]string, 1)}
	options.Ready = output
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- RunNative(ctx, db, adapter, options) }()
	addresses := ready(t, output.addresses)
	if _, exists := addresses["compatibility"]; exists != buildmode.Developer {
		t.Fatal("compatibility listener does not match build interface", addresses)
	}
	config := trustedTransport(t, options.Directory)
	tokens := []string{passwordCredential(t, config, addresses["legacy_tls"], "alice"), passwordCredential(t, config, addresses["tls"], "bob")}
	a, b := protectedConnect(t, config, addresses["lobby"], tokens[0]), protectedConnect(t, config, addresses["lobby"], tokens[1])
	login(t, a, ids[0], true)
	login(t, b, ids[1], true)
	duplicate := protectedConnect(t, config, addresses["lobby"], tokens[0])
	login(t, duplicate, ids[0], false)
	if value := legacyRequest(t, addresses["http"], "f|116|u|Gameloft|"); value != legacy.Success(116, "2|") {
		t.Fatal("presence not isolated", value)
	}
	for _, connection := range []net.Conn{a, b} {
		if err := lobby.Write(connection, lobby.Frame{Opcode: lobby.KeepAliveRequest}); err != nil {
			t.Fatal(err)
		}
		frame, err := lobby.Read(connection)
		if err != nil || frame.Opcode != lobby.KeepAliveResponse {
			t.Fatal("duplicate disturbed live connection", err)
		}
	}
	a.Close()
	deadline := time.Now().Add(time.Second)
	for legacyRequest(t, addresses["http"], "f|116|u|Gameloft|") != legacy.Success(116, "1|") {
		if time.Now().After(deadline) {
			t.Fatal("disconnected account retained presence")
		}
		time.Sleep(5 * time.Millisecond)
	}
	a = protectedConnect(t, config, addresses["lobby"], tokens[0])
	login(t, a, ids[0], true)
	if value := legacyRequest(t, addresses["http"], "f|116|u|Gameloft|"); value != legacy.Success(116, "2|") {
		t.Fatal("rejoin did not restore only the disconnected account")
	}
	// A partial unauthenticated TLS handshake must not hold shutdown open.
	partial := connect(t, addresses["lobby"])
	if _, err := partial.Write([]byte{0}); err != nil {
		t.Fatal(err)
	}
	cancel()
	stopped(t, done, false)
	for _, connection := range []net.Conn{a, b, partial} {
		var buffer [1]byte
		_, err := connection.Read(buffer[:])
		if err == nil {
			t.Fatal("connection survived service exit")
		}
		if timeout, ok := err.(net.Error); ok && timeout.Timeout() {
			t.Fatal("connection leaked until client deadline")
		}
	}
	available(t, addresses)
	if err := accounts.Revoke(db, tokens[0]); err != nil {
		t.Fatal(err)
	}
	// Fresh service state must not resurrect presence or revoked credentials.
	ctx2, cancel2 := context.WithCancel(context.Background())
	defer cancel2()
	go func() { done <- RunNative(ctx2, db, adapter, options) }()
	addresses2 := ready(t, output.addresses)
	if value := legacyRequest(t, addresses2["http"], "f|116|u|Gameloft|"); value != legacy.Success(116, "0|") {
		t.Fatal("presence survived restart")
	}
	for _, invalid := range []string{tokens[0], ids[0]} {
		connection := protectedConnect(t, config, addresses2["lobby"], invalid)
		if _, err := lobby.Read(connection); err == nil {
			t.Fatal("revoked token or account UID admitted after restart")
		}
	}
	cancel2()
	stopped(t, done, false)
	available(t, addresses2)
}

func TestNativeStartupFailuresReleaseListeners(t *testing.T) {
	db, adapter, options, _ := setup(t)
	probe, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	first := probe.Addr().String()
	defer probe.Close()
	occupied, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer occupied.Close()
	probe.Close()
	options.HTTP = first
	options.Lobby = occupied.Addr().String()
	if err = RunNative(context.Background(), db, adapter, options); err == nil {
		t.Fatal("occupied last listener accepted")
	}
	available(t, map[string]string{"http": first})
	options.HTTP, options.Lobby = "127.0.0.1:0", "127.0.0.1:0"
	output := readiness{addresses: make(chan map[string]string, 1), fail: true}
	options.Ready = output
	done := make(chan error, 1)
	go func() { done <- RunNative(context.Background(), db, adapter, options) }()
	addresses := ready(t, output.addresses)
	stopped(t, done, true)
	available(t, addresses)
}

func TestDomainStartupPublishesHTTPSAndAuthenticatesLobby(t *testing.T) {
	db, adapter, options, ids := setup(t)
	issuer, err := loopback.Certificate(options.Directory)
	if err != nil {
		t.Fatal(err)
	}
	ca, err := x509.ParseCertificate(issuer.Certificate[0])
	if err != nil {
		t.Fatal(err)
	}
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	now := time.Now()
	leaf := &x509.Certificate{SerialNumber: big.NewInt(2), DNSNames: []string{"game.example.org"}, NotBefore: now.Add(-time.Hour), NotAfter: now.Add(time.Hour), KeyUsage: x509.KeyUsageDigitalSignature, ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}}
	der, err := x509.CreateCertificate(rand.Reader, leaf, ca, &key.PublicKey, issuer.PrivateKey)
	if err != nil {
		t.Fatal(err)
	}
	keyDER, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		t.Fatal(err)
	}
	chain := append(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}), pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: issuer.Certificate[0]})...)
	for name, body := range map[string][]byte{"fullchain.pem": chain, "privkey.pem": pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: keyDER})} {
		if err = os.WriteFile(filepath.Join(options.Directory, name), body, 0600); err != nil {
			t.Fatal(err)
		}
	}
	options.Domain = "game.example.org"
	options.ResolveIPv4 = func(context.Context, string) ([]netip.Addr, error) {
		return []netip.Addr{netip.MustParseAddr("127.0.0.1")}, nil
	}
	output := readiness{addresses: make(chan map[string]string, 1)}
	options.Ready = output
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- RunNative(ctx, db, adapter, options) }()
	addresses := ready(t, output.addresses)
	if len(addresses) != 5 || !strings.HasPrefix(addresses["tls"], "https://game.example.org:") || !strings.HasPrefix(addresses["lobby"], "game.example.org:") {
		t.Fatal("domain discovery mismatch", addresses)
	}
	localEndpoint := func(endpoint string) string {
		_, port, err := net.SplitHostPort(strings.TrimPrefix(endpoint, "https://"))
		if err != nil {
			t.Fatal(err)
		}
		return net.JoinHostPort("127.0.0.1", port)
	}
	config := trustedTransport(t, options.Directory)
	config.ServerName = options.Domain
	token := passwordCredential(t, config, "https://"+localEndpoint(addresses["tls"]), "alice")
	connection := protectedConnect(t, config, localEndpoint(addresses["lobby"]), token)
	login(t, connection, ids[0], true)
	cancel()
	stopped(t, done, false)
}
