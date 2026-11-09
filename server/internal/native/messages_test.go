package native

import (
	"net/http/httptest"
	"strconv"
	"strings"
	"testing"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/protocol/legacy"
)

func TestNativeMailboxPersistenceReadDeleteAndIsolation(t *testing.T) {
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
	if result := call("f|77|u|" + a + "|n|bob|t|4|l|Join my room|o|room-17|"); result != legacy.Success(77, "") {
		t.Fatal(result)
	}
	var id int
	if err = s.Store.DB.QueryRow("SELECT id FROM messages WHERE sender=?", alice).Scan(&id); err != nil {
		t.Fatal(err)
	}
	m := strconv.Itoa(id)
	if result := call("f|74|u|" + b + "|r|2|"); !strings.Contains(result, "4^1^0^1|") || !strings.Contains(result, "32^1^0^1|") {
		t.Fatal(result)
	}
	if result := call("f|75|u|" + b + "|t|4|r|0|a|0|c|10|"); !strings.Contains(result, "c|1|m|"+m+"^4^Join my room^alice^") {
		t.Fatal(result)
	}
	if result := call("f|91|u|" + a + "|"); result != legacy.Success(91, "1|") {
		t.Fatal(result)
	}
	for _, fields := range []string{"f|76|u|" + a + "|m|" + m + "|", "f|93|u|" + b + "|m|" + m + "|", "f|78|u|" + a + "|m|" + m + "|", "f|94|u|" + b + "|m|" + m + "|"} {
		if result := call(fields); !strings.Contains(result, "|r|e|") {
			t.Fatal("mailbox ownership bypass", result)
		}
	}
	if result := call("f|93|u|" + a + "|m|" + m + "|"); result != legacy.Success(93, "room-17") {
		t.Fatal(result)
	}
	if result := call("f|76|u|" + b + "|m|" + m + "|"); result != legacy.Success(76, "room-17") {
		t.Fatal(result)
	}
	if result := call("f|74|u|" + b + "|"); !strings.Contains(result, "4^0^1^1|") {
		t.Fatal(result)
	}
	for _, extra := range []string{"t|3|l|Hi|o|body|", "t|1|l|Hi|o|body|", "t|4|l|bad^title|o|body|", "t|4|l|" + strings.Repeat("a", 97) + "|o|body|"} {
		if result := call("f|77|u|" + a + "|n|bob|" + extra); result != legacy.Failure(77, 40) {
			t.Fatal(result)
		}
	}
	for _, page := range []string{"a|-1|", "c|101|", "t|1,1|", "r|3|"} {
		if result := call("f|75|u|" + b + "|" + page); result != legacy.Failure(75, 40) {
			t.Fatal(result)
		}
	}
	if err = s.Store.Close(); err != nil {
		t.Fatal(err)
	}
	s.Store, err = persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Store.Close()
	if result := call("f|92|u|" + a + "|"); !strings.Contains(result, "^Join my room^") || !strings.Contains(result, "^bob^") {
		t.Fatal("sent message lost", result)
	}
	if result := call("f|78|u|" + b + "|m|" + m + "|"); result != legacy.Success(78, "") {
		t.Fatal(result)
	}
	if result := call("f|75|u|" + b + "|"); result != legacy.Success(75, "c|0|m|") {
		t.Fatal(result)
	}
	if result := call("f|93|u|" + a + "|m|" + m + "|"); result != legacy.Success(93, "room-17") {
		t.Fatal("recipient deleted sender copy", result)
	}
	if result := call("f|94|u|" + a + "|m|" + m + "|"); result != legacy.Success(94, "") {
		t.Fatal(result)
	}
	var count int
	if err = s.Store.DB.QueryRow("SELECT count(*) FROM messages").Scan(&count); err != nil || count != 0 {
		t.Fatal("deleted message retained", count, err)
	}
}
