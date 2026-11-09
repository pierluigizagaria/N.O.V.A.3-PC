package native

import (
	"net/http/httptest"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/protocol/legacy"
)

func TestNativeKeepAliveRenewsAndLogoutRevokes(t *testing.T) {
	s, account, _, _ := fixture(t)
	call := func(fields string) string {
		w := httptest.NewRecorder()
		s.Handler().ServeHTTP(w, httptest.NewRequest("GET", "/legacy?b="+legacy.Encode([]byte(fields)), nil))
		return w.Body.String()
	}
	uid := loginUID(t, call("f|15|u|alice|p|native-test-password|"))
	storage, err := accounts.Issue(s.Store, account, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if _, err = s.Store.DB.Exec("UPDATE sessions SET expires=? WHERE account=?", time.Now().Add(time.Minute).Unix(), account); err != nil {
		t.Fatal(err)
	}
	if call("f|16|u|"+uid+"|") != legacy.Success(16, "") {
		t.Fatal("native keepalive rejected")
	}
	var expiry int64
	if err = s.Store.DB.QueryRow("SELECT max(expires) FROM sessions WHERE account=?", account).Scan(&expiry); err != nil || expiry < time.Now().Add(59*time.Minute).Unix() {
		t.Fatal("credential not renewed", err)
	}
	if call("f|17|u|"+uid+"|") != legacy.Success(17, "") {
		t.Fatal("native logout rejected")
	}
	for _, token := range []string{uid, storage} {
		if _, err = accounts.Resolve(s.Store, token); err == nil {
			t.Fatal("logout retained credential")
		}
	}
	if s.Sessions.Valid(account, time.Now()) || s.Sessions.State(account, time.Now()) != 0 {
		t.Fatal("logout retained presence")
	}
	if call("f|16|u|"+uid+"|") != legacy.Failure(16, 40) {
		t.Fatal("keepalive resurrected logout")
	}
}
