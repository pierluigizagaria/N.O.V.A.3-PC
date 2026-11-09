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

func TestNativeRegistrationLoginPasswordChangeAndRestart(t *testing.T) {
	s, _, path, diagnostics := fixture(t)
	call := func(fields string) string {
		w := httptest.NewRecorder()
		s.Handler().ServeHTTP(w, httptest.NewRequest("GET", "/legacy?b="+legacy.Encode([]byte(fields)), nil))
		if w.Code != 200 {
			t.Fatal(w.Code)
		}
		return w.Body.String()
	}
	register := "f|11|i|28003|u|new-player|p|new-password|e|local@example.invalid|c|IT|ef|0|o|0|"
	if got := call(register); got != legacy.Success(11, "") {
		t.Fatal("registration", got)
	}
	id, err := accounts.Authenticate(s.Store, "new-player", "new-password")
	if err != nil {
		t.Fatal(err)
	}
	info, err := accounts.ReadInformation(s.Store, id)
	if err != nil || info.Email != "local@example.invalid" || info.Country != "IT" {
		t.Fatal("registration profile", info, err)
	}
	inventory, err := grants.Inventory(s.Store, id)
	if err != nil || len(inventory) == 0 {
		t.Fatal("missing initial catalogue grants", err)
	}
	if got := call(register); got != legacy.Failure(11, 40) {
		t.Fatal("duplicate registration", got)
	}
	if got := call(strings.Replace(register, "new-password", "replacement-password", 1)); got != legacy.Failure(11, 40) {
		t.Fatal("registration replaced credentials", got)
	}
	uid := loginUID(t, call("f|15|u|new-player|p|new-password|"))
	other, err := accounts.Issue(s.Store, id, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if got := call("f|50|u|" + uid + "|p|wrong-password|np|next-password|vp|next-password|"); got != legacy.Failure(50, 40) {
		t.Fatal("old password proof", got)
	}
	if got := call("f|50|u|" + uid + "|p|new-password|np|next-password|vp|different|"); got != legacy.Failure(50, 40) {
		t.Fatal("confirmation", got)
	}
	if _, err := accounts.Resolve(s.Store, uid); err != nil {
		t.Fatal("failed change revoked session")
	}
	if got := call("f|50|u|" + uid + "|p|new-password|np|next-password|vp|next-password|"); got != legacy.Success(50, "") {
		t.Fatal("password change", got)
	}
	for _, token := range []string{uid, other} {
		if _, err := accounts.Resolve(s.Store, token); err == nil {
			t.Fatal("old session survived password change")
		}
	}
	if s.Sessions.Valid(id, time.Now()) {
		t.Fatal("native session survived password change")
	}
	if got := call("f|15|u|new-player|p|new-password|"); got != legacy.Failure(15, 40) {
		t.Fatal("old password accepted")
	}
	loginUID(t, call("f|15|u|new-player|p|next-password|"))
	if _, err := accounts.Register(s.Store, "failed-player", "new-password", "missing-catalogue"); err == nil {
		t.Fatal("unknown catalogue")
	}
	var count int
	if err := s.Store.DB.QueryRow("SELECT count(*) FROM accounts WHERE name='failed-player'").Scan(&count); err != nil || count != 0 {
		t.Fatal("partial registration retained", count, err)
	}
	s.Store.Close()
	s.Store, err = persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { s.Store.Close() })
	if got, err := accounts.Authenticate(s.Store, "new-player", "next-password"); err != nil || got != id {
		t.Fatal("credential persistence", err)
	}
	if inventory2, err := grants.Inventory(s.Store, id); err != nil || len(inventory2) != len(inventory) {
		t.Fatal("grant persistence", err)
	}
	for _, secret := range []string{"new-password", "next-password", "local@example.invalid", uid, other} {
		if strings.Contains(diagnostics.String(), secret) {
			t.Fatal("private account data logged")
		}
	}
}
