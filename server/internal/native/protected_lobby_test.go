package native

import (
	"context"
	"crypto/tls"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/protocol/legacy"
	"nova3.test/backend/internal/protocol/lobby"
)

func TestProtectedLobbyRequiresCredentialAndExactAccount(t *testing.T) {
	s, account, _, diagnostics := fixture(t)
	certServer := httptest.NewTLSServer(s.Handler())
	defer certServer.Close()
	// The lobby must work directly after the existing password login, before
	// the native client performs its separate storage authorization request.
	response, err := certServer.Client().Get(certServer.URL + "/legacy?b=" + legacy.Encode([]byte("f|15|u|alice|p|native-test-password|")))
	if err != nil {
		t.Fatal(err)
	}
	io.Copy(io.Discard, response.Body)
	response.Body.Close()
	token := response.Header.Get("X-Nova-Session")
	if resolved, e := accounts.Resolve(s.Store, token); e != nil || resolved != account {
		t.Fatal("password-to-lobby credential missing")
	}
	listener, err := tls.Listen("tcp", "127.0.0.1:0", &tls.Config{Certificates: certServer.TLS.Certificates, MinVersion: tls.VersionTLS12})
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() { done <- s.ServeProtectedLobby(ctx, listener) }()
	defer func() {
		cancel()
		select {
		case <-done:
		case <-time.After(3 * time.Second):
			t.Error("protected listener leaked")
		}
	}()
	dial := func(credential string) *tls.Conn {
		t.Helper()
		config := certServer.Client().Transport.(*http.Transport).TLSClientConfig.Clone()
		c, err := tls.Dial("tcp", listener.Addr().String(), config)
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { c.Close() })
		c.SetDeadline(time.Now().Add(3 * time.Second))
		preface := append([]byte("NV3L\x01"), []byte(credential)...)
		preface = append(preface, 0, 0, 0, 1)
		if _, err = c.Write(preface); err != nil {
			t.Fatal(err)
		}
		return c
	}
	// A stable account UID has the same length as a token but is not a token.
	wrong := dial(account)
	if _, err = lobby.Read(wrong); err == nil {
		t.Fatal("UID admitted as credential")
	}
	wrong.Close()
	login := func(c net.Conn, uid string) error {
		if err := lobby.Write(c, lobby.Frame{Opcode: lobby.EstablishRequest, Payload: make([]byte, 4)}); err != nil {
			return err
		}
		if _, err := lobby.Read(c); err != nil {
			return err
		}
		body := append(make([]byte, 4), lobby.String(uid)...)
		body = append(body, 0, 0, 0, 0, 1)
		if err := lobby.Write(c, lobby.Frame{Opcode: lobby.LoginRequest, Payload: body}); err != nil {
			return err
		}
		_, err := lobby.Read(c)
		return err
	}
	other := dial(token)
	if err = login(other, "someone-else"); err == nil {
		t.Fatal("credential admitted another UID")
	}
	other.Close()
	valid := dial(token)
	if err = login(valid, token); err != nil {
		t.Fatal(err)
	}
	if err = accounts.Revoke(s.Store, token); err != nil {
		t.Fatal(err)
	}
	lobby.Write(valid, lobby.Frame{Opcode: lobby.KeepAliveRequest})
	if _, err = lobby.Read(valid); err == nil {
		t.Fatal("revoked lobby credential retained")
	}
	valid.Close()
	// The listener must not interpret native/plaintext bytes as an auth bypass.
	plain, err := net.Dial("tcp", listener.Addr().String())
	if err != nil {
		t.Fatal(err)
	}
	plain.SetDeadline(time.Now().Add(3 * time.Second))
	plain.Write([]byte("native plaintext"))
	if _, err = lobby.Read(plain); err == nil {
		t.Fatal("plaintext accepted")
	}
	plain.Close()
	// Wait for handlers before inspecting their shared diagnostic buffer.
	cancel()
	select {
	case <-done:
		done <- nil
	case <-time.After(3 * time.Second):
		t.Fatal("handlers did not close")
	}
	if strings.Contains(diagnostics.String(), token) {
		t.Fatal("credential leaked into log")
	}
}
