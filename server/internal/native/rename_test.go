package native

import (
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/protocol/legacy"
)

func TestNativeRenamePreservesIdentityAndRevokesSessions(t *testing.T) {
	s, _, path, diagnostics := fixture(t)
	call := func(fields string) string {
		w := httptest.NewRecorder()
		s.Handler().ServeHTTP(w, httptest.NewRequest("GET", "/legacy?b="+legacy.Encode([]byte(fields)), nil))
		if w.Code != 200 {
			t.Fatal(w.Code)
		}
		return w.Body.String()
	}
	for _, name := range []string{"rename-before", "rename-taken"} {
		if got := call("f|11|i|28003|u|" + name + "|p|rename-password|e||c||ef|0|o|0|"); got != legacy.Success(11, "") {
			t.Fatal(got)
		}
	}
	id, err := accounts.Authenticate(s.Store, "rename-before", "rename-password")
	if err != nil {
		t.Fatal(err)
	}
	number, _, err := accounts.PublicIdentity(s.Store, id)
	if err != nil {
		t.Fatal(err)
	}
	lobbyID, _, err := accounts.LobbyIdentity(s.Store, id)
	if err != nil {
		t.Fatal(err)
	}
	inventory, err := grants.Inventory(s.Store, id)
	if err != nil {
		t.Fatal(err)
	}
	uid := loginUID(t, call("f|15|u|rename-before|p|rename-password|"))
	storage, err := accounts.Issue(s.Store, id, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	for _, suffix := range []string{
		"p|wrong|nu|rename-after|o|0|", "p|rename-password|nu|rename-taken|o|0|",
		"p|rename-password|nu|rename-after|o|1|", "p|rename-password|nu|name^invalid|o|0|",
		"p|rename-password|nu|rename-after|o|0|n|rename-taken|",
	} {
		if got := call("f|14|u|" + uid + "|" + suffix); got != legacy.Failure(14, 40) {
			t.Fatal("invalid rename", got)
		}
		if _, err := accounts.Resolve(s.Store, uid); err != nil {
			t.Fatal("failed rename revoked session", err)
		}
	}
	if got := call("f|14|u|" + uid + "|p|rename-password|nu|rename-after|o|0|"); got != legacy.Success(14, "") {
		t.Fatal(got)
	}
	for _, token := range []string{uid, storage} {
		if _, err := accounts.Resolve(s.Store, token); err == nil {
			t.Fatal("rename retained credential")
		}
	}
	if s.Sessions.Valid(id, time.Now()) {
		t.Fatal("rename retained lobby login")
	}
	if _, err := accounts.Authenticate(s.Store, "rename-before", "rename-password"); err == nil {
		t.Fatal("old name accepted")
	}
	s.Store.Close()
	s.Store, err = persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { s.Store.Close() })
	if got, err := accounts.Authenticate(s.Store, "rename-after", "rename-password"); err != nil || got != id {
		t.Fatal("renamed account", err)
	}
	if got, name, err := accounts.PublicIdentity(s.Store, id); err != nil || got != number || name != "rename-after" {
		t.Fatal("public identity", got, name, err)
	}
	if got, name, err := accounts.LobbyIdentity(s.Store, id); err != nil || got != lobbyID || name != "rename-after" {
		t.Fatal("lobby identity", got, name, err)
	}
	if got, err := grants.Inventory(s.Store, id); err != nil || len(got) != len(inventory) {
		t.Fatal("inventory", err)
	}
	for _, secret := range []string{"rename-password", uid, storage} {
		if strings.Contains(diagnostics.String(), secret) {
			t.Fatal("rename logged secrets")
		}
	}
}
