package native

import (
	"fmt"
	"net/http/httptest"
	"strconv"
	"strings"
	"testing"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/protocol/legacy"
)

func TestPublicSDKStoreOwnershipQuotaAndRestart(t *testing.T) {
	s, alice, path, _ := fixture(t)
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
	if got := call("f|11|u|bob|p|bob-password|"); got != legacy.Success(11, "") {
		t.Fatal(got)
	}
	b := loginUID(t, call("f|15|u|bob|p|bob-password|"))
	if got := call("f|66|u|" + a + "|k|key2_01|d|public-only|"); got != legacy.Success(66, "") {
		t.Fatal(got)
	}
	var private int
	if err := s.Store.DB.QueryRow("SELECT count(*) FROM native_data WHERE account=?", alice).Scan(&private); err != nil || private != 0 {
		t.Fatal("SDK store contaminated Seshat", err)
	}
	number, _, err := accounts.PublicIdentity(s.Store, alice)
	if err != nil {
		t.Fatal(err)
	}
	for _, selector := range []string{"n|alice|", "un|" + strconv.FormatInt(number, 10) + "|"} {
		if got := call("f|65|u|" + b + "|k|key2_01|" + selector); got != legacy.Success(65, "n|alice|public-only|") {
			t.Fatal("public read", got)
		}
		if got := call("f|66|u|" + b + "|k|key2_01|d|bad|" + selector); got != legacy.Failure(66, 40) {
			t.Fatal("foreign write", got)
		}
		if got := call("f|72|u|" + b + "|k|key2_01|" + selector); got != legacy.Failure(72, 40) {
			t.Fatal("foreign delete", got)
		}
	}
	for _, fields := range []string{"k||d|x|", "k|safe|", "k|safe|d|" + strings.Repeat("x", 2049) + "|"} {
		if got := call("f|66|u|" + a + "|" + fields); got != legacy.Failure(66, 40) {
			t.Fatal("invalid value", got)
		}
	}
	if publicDataValue("line\nvalue") || publicDataValue("a|b") || publicDataValue(string([]byte{255})) {
		t.Fatal("unsafe native data accepted")
	}
	for i := 1; i < maxPublicUserKeys; i++ {
		if got := call(fmt.Sprintf("f|66|u|%s|k|key%d|d|value|", a, i)); got != legacy.Success(66, "") {
			t.Fatal("quota fill", got)
		}
	}
	if got := call("f|66|u|" + a + "|k|overflow|d|value|"); got != legacy.Failure(66, 40) {
		t.Fatal("quota exceeded", got)
	}
	if got := call("f|66|u|" + a + "|k|key2_01|d|n|"); got != legacy.Success(66, "") {
		t.Fatal("quota update", got)
	}
	s.Store.Close()
	s.Store, err = persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { s.Store.Close() })
	if got := call("f|65|u|" + b + "|k|key2_01|n|alice|"); got != legacy.Success(65, "n|alice|n|") {
		t.Fatal("persistence/ambiguous literal", got)
	}
	for i := 0; i < 2; i++ {
		if got := call("f|72|u|" + a + "|k|key2_01|"); got != legacy.Success(72, "") {
			t.Fatal("idempotent removal", got)
		}
	}
	if got := call("f|65|u|" + b + "|k|key2_01|n|alice|"); got != legacy.Success(65, "n|alice||") {
		t.Fatal("missing value", got)
	}
	if got := call("f|66|u|" + a + "|k|overflow|d||"); got != legacy.Success(66, "") {
		t.Fatal("released quota", got)
	}
}
