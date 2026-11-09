package native

import (
	"io"
	"net/http/httptest"
	"strings"
	"testing"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/protocol/legacy"
)

func TestPasswordLoginIssuesTransportCredentialOnlyOverTLS(t *testing.T) {
	s, account, _, logs := fixture(t)
	secure := httptest.NewTLSServer(s.Handler())
	defer secure.Close()
	plain := httptest.NewServer(s.Handler())
	defer plain.Close()
	for _, test := range []struct{ tls, valid bool }{{true, true}, {true, false}, {false, true}} {
		endpoint, client := plain.URL, plain.Client()
		if test.tls {
			endpoint, client = secure.URL, secure.Client()
		}
		password := "incorrect"
		if test.valid {
			password = "native-test-password"
		}
		response, err := client.Get(endpoint + "/legacy?b=" + legacy.Encode([]byte("f|15|u|alice|p|"+password+"|")))
		if err != nil {
			t.Fatal(err)
		}
		body, err := io.ReadAll(response.Body)
		response.Body.Close()
		if err != nil {
			t.Fatal(err)
		}
		token, id := response.Header.Get("X-Nova-Session"), response.Header.Get("X-Nova-Account")
		if test.tls && test.valid {
			resolved, err := accounts.Resolve(s.Store, token)
			if err != nil || resolved != account || id != account {
				t.Fatal("password response did not bind a transport credential")
			}
			if string(body) != legacy.Success(15, "u|"+token+"|t|1|un|1|") {
				t.Fatal("native login response changed")
			}
			if strings.Contains(logs.String(), token) {
				t.Fatal("transport credential leaked into logs")
			}
			if err := accounts.Revoke(s.Store, token); err != nil {
				t.Fatal(err)
			}
			if _, err := accounts.Resolve(s.Store, token); err == nil {
				t.Fatal("transport credential not revocable")
			}
		} else if token != "" || id != "" {
			t.Fatal("credential issued for insecure or rejected login")
		}
	}
}

// The native response has a fixed envelope followed by ordinary pipe fields.
func loginUID(t *testing.T, body string) string {
	t.Helper()
	prefix := legacy.Success(15, "u|")
	if !strings.HasPrefix(body, prefix) {
		t.Fatalf("login failed: %q", body)
	}
	uid, _, ok := strings.Cut(strings.TrimPrefix(body, prefix), "|")
	if !ok || len(uid) != 64 {
		t.Fatal("invalid native UID")
	}
	return uid
}

func TestLegacySessionCannotBeReplacedByPublicIdentity(t *testing.T) {
	s, account, _, _ := fixture(t)
	h := s.Handler()
	call := func(fields string) string {
		w := httptest.NewRecorder()
		h.ServeHTTP(w, httptest.NewRequest("GET", "/legacy?b="+legacy.Encode([]byte(fields)), nil))
		return w.Body.String()
	}
	uid := loginUID(t, call("f|15|u|alice|p|native-test-password|"))
	if call("f|127|u|"+uid+"|") == legacy.Failure(127, 40) {
		t.Fatal("valid session rejected")
	}
	for _, invalid := range []string{account, "1", "p1"} {
		if call("f|127|u|"+invalid+"|") != legacy.Failure(127, 40) {
			t.Fatal("public identity authorized request")
		}
	}
	if err := accounts.Revoke(s.Store, uid); err != nil {
		t.Fatal(err)
	}
	if call("f|127|u|"+uid+"|") != legacy.Failure(127, 40) {
		t.Fatal("revoked native session accepted")
	}
}
