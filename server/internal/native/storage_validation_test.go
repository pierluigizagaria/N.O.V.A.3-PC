package native

import (
	"nova3.test/backend/internal/accounts"
	"strings"
	"testing"
	"time"
)

func TestStorageRejectsUnsafeNativeScalarAndFixedArrayRecords(t *testing.T) {
	info := "1|2|3|4|5||0^0^0^0^0^0^0^0||" + strings.TrimSuffix(strings.Repeat("0^", 16), "^")
	achievements := strings.Repeat("0|", 70) + strings.Repeat("0", 96)
	for key, text := range map[string]string{"key1_01": info, "key3_01": achievements} {
		if err := validateData(key, text); err != nil {
			t.Fatal(err)
		}
		for _, bad := range []string{"0^1" + text[1:], "" + text[1:]} {
			if err := validateData(key, bad); err == nil {
				t.Fatal("invalid scalar accepted", key, bad)
			}
		}
	}
	for _, bad := range []string{strings.Replace(info, "1|", "35791395|", 1), strings.Replace(info, "0^0^0^0^0^0^0^0|", "0^0|", 1)} {
		if err := validateData("key1_01", bad); err == nil {
			t.Fatal("unsafe profile accepted")
		}
	}
}

func TestStorageRefreshesValidTokenWithoutRevivingRevocation(t *testing.T) {
	s, account, _, _ := fixture(t)
	token, err := accounts.Issue(s.Store, account, time.Minute)
	if err != nil {
		t.Fatal(err)
	}
	store := Storage{DB: s.Store, Shop: s.Shop}
	if _, err = store.Access(token, "me", "key1_01", nil); err != nil {
		t.Fatal(err)
	}
	var expiry int64
	if err = s.Store.DB.QueryRow("SELECT max(expires) FROM sessions WHERE account=?", account).Scan(&expiry); err != nil || expiry < time.Now().Add(59*time.Minute).Unix() {
		t.Fatal("storage token not renewed", expiry, err)
	}
	if err = accounts.Revoke(s.Store, token); err != nil {
		t.Fatal(err)
	}
	if _, err = store.Access(token, "me", "key1_01", nil); err == nil {
		t.Fatal("revoked token revived")
	}
}
