package shop_test

import (
	"net/http/httptest"
	"strings"
	"testing"

	"nova3.test/backend/internal/adapters/shop"
)

func TestNativeSixPresetSeedAndLegacySelectionPreservation(t *testing.T) {
	s, a, token, bob, _ := fixture(t)
	server := httptest.NewServer(shop.Handler(s, a))
	defer server.Close()
	endpoint := server.URL + "/compat/v1/purchases?version=" + a.Catalogue.Version
	seed := strings.Split(request(t, endpoint, "GET", token, "", 200), "|")
	if p := strings.Split(seed[8], "^"); len(p) != 10 || p[0] != "6" {
		t.Fatal("native menu would reset a non-six-preset seed", seed[8])
	}
	var alice string
	must(t, s.DB.QueryRow("SELECT id FROM accounts WHERE id<>?", bob).Scan(&alice))
	// An existing short record must expand without losing its selected preset,
	// signature, background or skins. Reading does not rewrite stored evidence.
	old := "2^1#0#0#2#0#0#1#2#0#1^2#0#0#1#0#0#2#1#0#0^1^1^1"
	mustExec := func(query string, args ...any) { _, err := s.DB.Exec(query, args...); must(t, err) }
	mustExec("INSERT INTO native_equipment(account,equipment,skins) VALUES(?,?,?)", alice, old, "2^255^-1^1")
	got := strings.Split(request(t, endpoint, "GET", token, "", 200), "|")
	p := strings.Split(got[8], "^")
	before := strings.Split(old, "^")
	if len(p) != 10 || p[0] != "6" || p[1] != before[1] || p[2] != before[2] || strings.Join(p[7:], "^") != "1^1^1" || got[11] != "2^255^-1^1" {
		t.Fatal("legacy selection changed", got[8], got[11])
	}
	var stored string
	must(t, s.DB.QueryRow("SELECT equipment FROM native_equipment WHERE account=?", alice).Scan(&stored))
	if stored != old {
		t.Fatal("read modified the stored legacy record")
	}
	// Reject a seventh slot, an out-of-range active slot and malformed records.
	for _, invalid := range []string{
		"7^" + strings.Repeat(p[1]+"^", 7) + "0^0^0",
		"6^" + strings.Repeat(p[1]+"^", 6) + "6^0^0",
		"6^" + strings.Repeat(p[1]+"^", 5) + "0^0^0",
	} {
		bad := append([]string{}, got...)
		bad[8] = invalid
		request(t, endpoint, "PUT", token, strings.Join(bad, "|"), 400)
		if actual := request(t, endpoint, "GET", token, "", 200); actual != strings.Join(got, "|") {
			t.Fatal("rejected write changed equipment")
		}
	}
	// Locked slots still have records, but only the two included slots can be
	// selected without preset entitlements.
	for _, item := range a.Groups["presets"] {
		mustExec("DELETE FROM entitlements WHERE account=? AND item=?", alice, item.ID)
	}
	free := request(t, endpoint, "GET", token, "", 200)
	request(t, endpoint, "PUT", token, free, 200)
	locked := strings.Split(free, "|")
	parts := strings.Split(locked[8], "^")
	parts[7] = "5"
	locked[8] = strings.Join(parts, "^")
	request(t, endpoint, "PUT", token, strings.Join(locked, "|"), 400)
	if actual := request(t, endpoint, "GET", token, "", 200); actual != free {
		t.Fatal("locked-slot write changed equipment")
	}
}
