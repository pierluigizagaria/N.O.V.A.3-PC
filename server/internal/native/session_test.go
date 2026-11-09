package native

import (
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strings"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/protocol/lobby"
)

func TestLobbyExpiryClosesIdleAuthenticatedConnection(t *testing.T) {
	s, account, _, _ := fixture(t)
	if err := s.Sessions.Login(account, time.Now(), 200*time.Millisecond); err != nil {
		t.Fatal(err)
	}
	client, server := net.Pipe()
	defer client.Close()
	done := make(chan struct{})
	go func() { s.lobbyConnection(server); close(done) }()
	client.SetDeadline(time.Now().Add(3 * time.Second))
	if err := lobby.Write(client, lobby.Frame{Opcode: lobby.EstablishRequest, Payload: make([]byte, 4)}); err != nil {
		t.Fatal(err)
	}
	if _, err := lobby.Read(client); err != nil {
		t.Fatal(err)
	}
	payload := append(make([]byte, 4), lobby.String(account)...)
	payload = append(payload, 0, 0, 0, 0, 1)
	if err := lobby.Write(client, lobby.Frame{Opcode: lobby.LoginRequest, Payload: payload}); err != nil {
		t.Fatal(err)
	}
	if _, err := lobby.Read(client); err != nil {
		t.Fatal(err)
	}
	if _, err := lobby.Read(client); err != io.EOF {
		t.Fatalf("expected expiration EOF, got %v", err)
	}
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("expired handler did not exit")
	}
	if s.Sessions.Count(time.Now()) != 0 || s.legacyAccount(account) {
		t.Fatal("expired presence retained")
	}
}

func TestConcurrentNativeEquipmentIsolationAndFreshServiceReload(t *testing.T) {
	s, alice, path, _ := fixture(t)
	bob, err := accounts.Create(s.Store, "bob")
	if err != nil {
		t.Fatal(err)
	}
	if err = grants.Provision(s.Store, bob.ID, s.Shop.Catalogue.Version); err != nil {
		t.Fatal(err)
	}
	ids := []string{alice, bob.ID}
	tokens, expected := make([]string, 2), make([]string, 2)
	server := httptest.NewTLSServer(s.Handler())
	defer server.Close()
	for i, id := range ids {
		tokens[i], err = accounts.Issue(s.Store, id, time.Hour)
		if err != nil {
			t.Fatal(err)
		}
		body := request(t, server.Client(), "GET", server.URL+"/data/me/key2_01?access_token="+tokens[i], "", 200)
		fields := strings.Split(body, "|")
		if i == 0 {
			fields[8] = "6^" + strings.Repeat("2#0#0#1#0#0#2#1#0#0^", 6) + "5^0^1"
			fields[11] = "2^255^-1^1"
		} else {
			fields[8] = "6^" + strings.Repeat("1#0#0#2#0#0#1#2#0#1^", 6) + "5^0^0"
			fields[11] = "2^1^2^0"
		}
		expected[i] = strings.Join(fields, "|")
	}
	// Parallel real native-shaped HTTP writes use the same account-bound path as
	// the game. No administrative equip/provision/reset runs before the reload.
	t.Run("concurrent writes", func(t *testing.T) {
		for i, name := range []string{"alice", "bob"} {
			t.Run(name, func(t *testing.T) {
				t.Parallel()
				form := url.Values{"access_token": {tokens[i]}, "data": {expected[i]}, "visibility": {"private"}}
				if got := request(t, server.Client(), "POST", server.URL+"/data/me/key2_01?"+form.Encode(), "", 200); got != expected[i] {
					t.Fatal("selection not retained")
				}
			})
		}
	})
	// An expired storage token may not overwrite either account's selection.
	if _, err = s.Store.DB.Exec("UPDATE sessions SET expires=0"); err != nil {
		t.Fatal(err)
	}
	for i := range ids {
		form := url.Values{"access_token": {tokens[i]}, "data": {expected[1-i]}}
		request(t, server.Client(), "POST", server.URL+"/data/me/key2_01?"+form.Encode(), "", http.StatusBadRequest)
	}
	server.Close()
	if err = s.Store.Close(); err != nil {
		t.Fatal(err)
	}
	db, err := persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	// Reconstruct the service, rather than retain in-memory session/presence state.
	next := &Services{Store: db, Shop: s.Shop, Endpoints: s.Endpoints, Log: s.Log}
	restarted := httptest.NewTLSServer(next.Handler())
	defer restarted.Close()
	for i, id := range ids {
		token, err := accounts.Issue(db, id, time.Hour)
		if err != nil {
			t.Fatal(err)
		}
		if got := request(t, restarted.Client(), "GET", restarted.URL+"/data/me/key2_01?access_token="+token, "", 200); got != expected[i] {
			t.Fatal("account selection changed across restart")
		}
	}
}
