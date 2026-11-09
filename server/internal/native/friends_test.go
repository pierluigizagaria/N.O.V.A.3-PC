package native

import (
	"net/http/httptest"
	"strconv"
	"strings"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/protocol/legacy"
)

func TestNativeContactsPersistenceIsolationAndPresence(t *testing.T) {
	s, alice, path, _ := fixture(t)
	bob, err := accounts.Create(s.Store, "bob")
	if err != nil {
		t.Fatal(err)
	}
	if err = grants.Provision(s.Store, bob.ID, s.Shop.Catalogue.Version); err != nil {
		t.Fatal(err)
	}
	if err = accounts.SetPassword(s.Store, bob.ID, "bob-password"); err != nil {
		t.Fatal(err)
	}
	call := func(fields string) string {
		t.Helper()
		w := httptest.NewRecorder()
		s.Handler().ServeHTTP(w, httptest.NewRequest("GET", "/legacy?b="+legacy.Encode([]byte(fields)), nil))
		if w.Code != 200 {
			t.Fatal(w.Code)
		}
		return w.Body.String()
	}
	a := loginUID(t, call("f|15|u|alice|p|native-test-password|"))
	b := loginUID(t, call("f|15|u|bob|p|bob-password|"))
	number, _, err := accounts.PublicIdentity(s.Store, bob.ID)
	if err != nil || number <= 1 {
		t.Fatal("public user numbers collide", err)
	}
	for _, selector := range []string{"n|bob|", "un|" + strconv.FormatInt(number, 10) + "|"} {
		if call("f|59|u|"+a+"|"+selector) != legacy.Success(59, "") {
			t.Fatal("add contact")
		}
	}
	if call("f|73|u|"+a+"|") != legacy.Success(73, "1|") {
		t.Fatal("duplicate contact or count format")
	}
	if call("f|61|u|"+b+"|") != legacy.Success(61, "c|0|f|") {
		t.Fatal("contact leaked into another owner's list")
	}
	if call("f|59|u|"+alice+"|n|bob|") != legacy.Failure(59, 40) {
		t.Fatal("stable account ID authorized mutation")
	}
	for _, selector := range []string{"n|alice|", "un|-1|", "n|missing|", "n|bob|un|2|"} {
		if call("f|59|u|"+a+"|"+selector) != legacy.Failure(59, 40) {
			t.Fatal("invalid selector accepted", selector)
		}
	}
	if call("f|97|u|"+b+"|a|2|") != legacy.Success(97, "") {
		t.Fatal("presence update")
	}
	if got := call("f|64|u|" + a + "|n|bob|"); got != legacy.Success(64, "n|bob|t|2|g|28003|a|28003|") {
		t.Fatal("public presence", got)
	}
	if got := call("f|64|u|" + a + "|un|" + strconv.FormatInt(number, 10) + "|"); got != legacy.Success(64, "n|bob|t|2|g|28003|a|28003|") {
		t.Fatal("public presence by number", got)
	}
	if got := call("f|64|u|" + a + "|n|missing|"); got != legacy.Failure(64, 40) {
		t.Fatal("missing presence", got)
	}
	list := call("f|61|u|" + a + "|s|0|c|0|hp|1|")
	if !strings.Contains(list, "^bob^^0,0,0^0^2^28003^0^en^local-default^0^1|") {
		t.Fatal("native nonempty list", list)
	}
	for _, page := range []string{"s|-1|", "c|101|", "hp|2|"} {
		if call("f|61|u|"+a+"|"+page) != legacy.Failure(61, 40) {
			t.Fatal("unsafe page accepted")
		}
	}
	if call("f|61|u|"+a+"|s|1|c|1|") != legacy.Success(61, "c|0|f|") {
		t.Fatal("contact pagination")
	}
	if s.Sessions.State(bob.ID, time.Now().Add(2*time.Hour)) != 0 {
		t.Fatal("expired presence remained online")
	}
	if err = s.Store.Close(); err != nil {
		t.Fatal(err)
	}
	s.Store, err = persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Store.Close()
	entries, err := accounts.Contacts(s.Store, alice, 0, 0)
	if err != nil || len(entries) != 1 || entries[0].Number != number {
		t.Fatal("contacts lost after restart", err)
	}
	// New logins after expiry/restart retain stable public identities.
	a = loginUID(t, call("f|15|u|alice|p|native-test-password|"))
	if call("f|60|u|"+a+"|n|bob|") != legacy.Success(60, "") || call("f|73|u|"+a+"|") != legacy.Success(73, "0|") {
		t.Fatal("delete contact")
	}
}
