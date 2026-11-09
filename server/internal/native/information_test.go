package native

import (
	"net/http/httptest"
	"strconv"
	"strings"
	"testing"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/protocol/legacy"
)

func TestNativeInformationPrivacyAtomicEditsAndRestart(t *testing.T) {
	s, alice, path, log := fixture(t)
	call := func(fields string) string {
		w := httptest.NewRecorder()
		s.Handler().ServeHTTP(w, httptest.NewRequest("GET", "/legacy?b="+legacy.Encode([]byte(fields)), nil))
		if w.Code != 200 {
			t.Fatal(w.Code)
		}
		return w.Body.String()
	}
	a := loginUID(t, call("f|15|u|alice|p|native-test-password|"))
	if got := call("f|11|u|bob|p|bob-password|"); got != legacy.Success(11, "") {
		t.Fatal(got)
	}
	b := loginUID(t, call("f|15|u|bob|p|bob-password|"))
	edit := "f|82|u|" + a + "|email|alice@example.invalid|email_flag|1|country|IT|firstname|Alice|lastname|Example|sex|1|birthday|2000-01-02|"
	if got := call(edit); got != legacy.Success(82, "") {
		t.Fatal("edit", got)
	}
	own := call("f|81|u|" + a + "|")
	if !strings.Contains(own, "email|alice@example.invalid|") || !strings.Contains(own, "firstname|Alice|") || strings.Contains(own, a) || strings.Contains(own, alice) {
		t.Fatal("owner profile", own)
	}
	number, _, err := accounts.PublicIdentity(s.Store, alice)
	if err != nil {
		t.Fatal(err)
	}
	for _, selector := range []string{"n|alice|", "un|" + strconv.FormatInt(number, 10) + "|"} {
		public := call("f|81|u|" + b + "|" + selector)
		if !strings.Contains(public, "country|IT|") || strings.Contains(public, "alice@example.invalid") || strings.Contains(public, "2000-01-02") || strings.Contains(public, "firstname|Alice|") || strings.Contains(public, a) {
			t.Fatal("private profile leaked", public)
		}
	}
	for _, fields := range []string{"n|bob|country|GB|", "country|GB|sex|2|", "email|invalid|", "firstname|" + strings.Repeat("x", 128) + "|"} {
		if got := call("f|82|u|" + a + "|" + fields); got != legacy.Failure(82, 40) {
			t.Fatal("invalid edit", got)
		}
	}
	if call("f|81|u|"+a+"|") != own {
		t.Fatal("failed edit partially committed")
	}
	if got := call("f|53|u|" + b + "|n|alice|nf|0|"); got != legacy.Success(53, "c|IT|") {
		t.Fatal("public country", got)
	}
	publicID, _, err := accounts.LobbyIdentity(s.Store, alice)
	if err != nil {
		t.Fatal(err)
	}
	if got := call("f|58|u|" + b + "|n|alice|"); got != legacy.Success(58, publicID) {
		t.Fatal("public UID", got)
	}
	if got := call("f|81|u|" + publicID + "|"); got != legacy.Failure(81, 40) {
		t.Fatal("public UID became a login credential", got)
	}
	if got := call("f|54|u|" + a + "|p|wrong-password|nc|GB|"); got != legacy.Failure(54, 40) {
		t.Fatal("password proof", got)
	}
	if got := call("f|54|u|" + a + "|p|native-test-password|nc|GB|"); got != legacy.Success(54, "") {
		t.Fatal("change country", got)
	}
	if got := call("f|51|u|" + a + "|p|native-test-password|ne|next@example.invalid|"); got != legacy.Success(51, "") {
		t.Fatal("change email", got)
	}
	if got := call("f|59|u|" + b + "|n|alice|"); got != legacy.Success(59, "") {
		t.Fatal("friend", got)
	}
	if got := call("f|61|u|" + b + "|"); !strings.Contains(got, "^alice^GB^") {
		t.Fatal("contact country", got)
	}
	if got := call("f|82|u|" + a + "|firstname||"); got != legacy.Success(82, "") {
		t.Fatal("clear field", got)
	}
	if strings.Contains(log.String(), "alice@example.invalid") || strings.Contains(log.String(), "2000-01-02") {
		t.Fatal("private fields logged")
	}
	if err := s.Store.Close(); err != nil {
		t.Fatal(err)
	}
	reopened, err := persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer reopened.Close()
	s.Store = reopened
	info, err := accounts.ReadInformation(reopened, alice)
	if err != nil || info.FirstName != "" || info.Country != "GB" || info.Email != "next@example.invalid" {
		t.Fatal("restart", info, err)
	}
	if _, err := reopened.DB.Exec("UPDATE account_information SET country=? WHERE account=?", strings.Repeat("x", 128), alice); err != nil {
		t.Fatal(err)
	}
	if got := call("f|81|u|" + a + "|"); got != legacy.Failure(81, 40) {
		t.Fatal("unsafe persisted profile served", got)
	}
}
