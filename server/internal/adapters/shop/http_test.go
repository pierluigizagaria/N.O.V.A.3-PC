package shop_test

import (
	"encoding/xml"
	"io"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/adapters/shop"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/testfixture"
)

func must(t *testing.T, e error) {
	t.Helper()
	if e != nil {
		t.Fatal(e)
	}
}
func fixture(t *testing.T) (*persistence.Store, *shop.Adapter, string, string, string) {
	t.Helper()
	c, m := testfixture.NativeCatalogue()
	a, e := shop.New(c, m)
	must(t, e)
	path := filepath.Join(t.TempDir(), "shop.db")
	s, e := persistence.Open(path)
	must(t, e)
	t.Cleanup(func() { s.Close() })
	must(t, s.Admit(c))
	alice, e := accounts.Create(s, "alice")
	must(t, e)
	bob, e := accounts.Create(s, "bob")
	must(t, e)
	for _, id := range []string{alice.ID, bob.ID} {
		must(t, grants.Provision(s, id, c.Version))
	}
	token, e := accounts.Issue(s, alice.ID, time.Hour)
	must(t, e)
	return s, a, token, bob.ID, path
}
func request(t *testing.T, url, method, token, body string, status int) string {
	t.Helper()
	r, e := http.NewRequest(method, url, strings.NewReader(body))
	must(t, e)
	if token != "" {
		r.Header.Set("Authorization", "Bearer "+token)
	}
	response, e := http.DefaultClient.Do(r)
	must(t, e)
	defer response.Body.Close()
	data, e := io.ReadAll(response.Body)
	must(t, e)
	if response.StatusCode != status {
		t.Fatalf("status %d, expected %d: %s", response.StatusCode, status, data)
	}
	return string(data)
}
func TestOriginalBodiesHTTPRoundtripAndRestart(t *testing.T) {
	s, a, token, bob, path := fixture(t)
	server := httptest.NewServer(shop.Handler(s, a))
	defer server.Close()
	endpoint := server.URL + "/compat/v1/purchases?version=" + a.Catalogue.Version
	original := request(t, endpoint, "GET", token, "", 200)
	fields := strings.Split(original, "|")
	if len(fields) != 12 || fields[1] != "TT" || fields[7] != "TTT" {
		t.Fatal(original)
	}
	fields[8] = "6^" + strings.Repeat("2#0#0#1#0#0#2#1#0#1^", 6) + "5^0^0"
	fields[11] = "2^255^-1^1"
	fields[9] = "3^3^3^3^3^3^3^3^3"
	updated := strings.Join(fields, "|")
	if actual := request(t, endpoint, "PUT", token, updated, 200); actual != updated {
		t.Fatal("native body not retained", actual)
	}
	btoken, e := accounts.Issue(s, bob, time.Hour)
	must(t, e)
	if request(t, endpoint, "GET", btoken, "", 200) != original {
		t.Fatal("cross-account mutation")
	}
	priceBody := request(t, server.URL+"/compat/v1/prices?version="+a.Catalogue.Version, "GET", token, "", 200)
	var doc struct {
		Items []struct {
			Price       int `xml:"price_value"`
			Requirement int `xml:"previous_price_value"`
		} `xml:"items>item"`
	}
	must(t, xml.Unmarshal([]byte(priceBody), &doc))
	if len(doc.Items) != 27 || doc.Items[12].Requirement != 20 {
		t.Fatal("price order/count", len(doc.Items), priceBody)
	}
	server.Close()
	must(t, s.Close())
	reopened, e := persistence.Open(path)
	must(t, e)
	defer reopened.Close()
	next := httptest.NewServer(shop.Handler(reopened, a))
	defer next.Close()
	if request(t, next.URL+"/compat/v1/purchases?version="+a.Catalogue.Version, "GET", token, "", 200) != updated {
		t.Fatal("native equipment lost on process/database restart")
	}
}
func TestRejectedRequestsDoNotChangeState(t *testing.T) {
	s, a, token, _, _ := fixture(t)
	server := httptest.NewServer(shop.Handler(s, a))
	defer server.Close()
	base := server.URL + "/compat/v1/purchases?version=" + a.Catalogue.Version
	original := request(t, base, "GET", token, "", 200)
	bads := []string{"", original + "|extra", strings.Replace(original, "|TT|", "|FT|", 1), strings.Replace(original, "6^1#", "999^1#", 1)}
	// The last quantity fails after earlier UPDATEs, exercising transaction rollback.
	lateFailure := strings.Split(original, "|")
	lateFailure[9] = "1^1^1^1^1^1^1^1^99999"
	bads = append(bads, strings.Join(lateFailure, "|"))
	for _, body := range bads {
		request(t, base, "PUT", token, body, 400)
		if request(t, base, "GET", token, "", 200) != original {
			t.Fatal("failed request changed state")
		}
	}
	request(t, base, "GET", "", "", 401)
	request(t, server.URL+"/compat/v1/purchases?version=stale", "GET", token, "", 409)
	request(t, base+"&account=bob", "GET", token, "", 400)
	request(t, base, "POST", token, "", 405)
	request(t, server.URL+"/unknown", "GET", token, "", 404)
	must(t, accounts.Revoke(s, token))
	request(t, base, "GET", token, "", 401)
}
func TestMappingMismatchAndSignatureTailRejected(t *testing.T) {
	_, a, _, _, _ := fixture(t)
	m := a.Mapping
	m.Resource = strings.Repeat("b", 64)
	if _, e := shop.New(a.Catalogue, m); e == nil {
		t.Fatal("accepted wrong resource")
	}
	m = a.Mapping
	m.Rows = append([]shop.Row{}, m.Rows...)
	for n := range m.Rows {
		if m.Rows[n].Native.Group == "signatures" && m.Rows[n].Native.Ordinal == 2 {
			m.Rows[n].ResourcePrice++
		}
	}
	if _, e := shop.New(a.Catalogue, m); e == nil {
		t.Fatal("accepted incompatible shared signature tail")
	}
}
func TestListenerIsLoopbackOnly(t *testing.T) {
	for _, address := range []string{"0.0.0.0:0", "localhost:0", "[::1]:0", "127.0.0.1:65536"} {
		listener, e := shop.ListenLocal(address)
		if listener != nil {
			listener.Close()
		}
		if e == nil {
			t.Fatal("accepted bind", address)
		}
	}
	listener, e := shop.ListenLocal("127.0.0.1:0")
	must(t, e)
	listener.Close()
}
func TestSchemaOneMigrationPreservesAccounts(t *testing.T) {
	s, _, _, _, path := fixture(t)
	_, e := s.DB.Exec("DROP TABLE match_progress; DROP TABLE wallets; DROP TABLE public_user_data; DROP TABLE account_information; DROP TABLE messages; DROP TABLE scored_matches; DROP TABLE player_stats; DROP TABLE contacts; DROP TABLE lobby_identities; DROP TABLE native_data; DROP TABLE credentials; DROP TABLE native_equipment; PRAGMA user_version=1;")
	must(t, e)
	must(t, s.Close())
	reopened, e := persistence.Open(path)
	must(t, e)
	defer reopened.Close()
	var n int
	must(t, reopened.DB.QueryRow("SELECT count(*) FROM accounts").Scan(&n))
	if n != 2 {
		t.Fatal("migration lost accounts")
	}
	must(t, reopened.DB.QueryRow("SELECT count(*) FROM native_equipment").Scan(&n))
}
