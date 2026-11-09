package native

import (
	"strings"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/grants"
)

func TestSelectedPlayerDetailsArePublicButEquipmentAndWritesArePrivate(t *testing.T) {
	s, alice, _, _ := fixture(t)
	bob, err := accounts.Create(s.Store, "bob")
	if err != nil {
		t.Fatal(err)
	}
	if err = grants.Provision(s.Store, bob.ID, s.Shop.Catalogue.Version); err != nil {
		t.Fatal(err)
	}
	a, err := accounts.Issue(s.Store, alice, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	b, err := accounts.Issue(s.Store, bob.ID, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	store := Storage{DB: s.Store, Shop: s.Shop}
	info := "1|2|3|4|5|1^2^3^4^5^6|0^0^0^0^0^0^0^0||" + strings.TrimSuffix(strings.Repeat("0^", 16), "^")
	achievements := strings.Repeat("0|", 70) + strings.Repeat("1", 96)
	for key, value := range map[string]string{"key1_01": info, "key3_01": achievements} {
		if _, err = store.Access(b, "me", key, &value); err != nil {
			t.Fatal(err)
		}
		got, err := store.Access(a, "gllive:bob", key, nil)
		if err != nil || got != value {
			t.Fatal("selected player profile unavailable", key, err)
		}
		if _, err = store.Access(a, "gllive:bob", key, &value); err == nil {
			t.Fatal("cross-account write accepted")
		}
		if _, err = store.Access("invalid", "gllive:bob", key, nil); err == nil {
			t.Fatal("anonymous profile access accepted")
		}
	}
	if _, err = store.Access(a, "gllive:bob", "key2_01", nil); err == nil {
		t.Fatal("private equipment exposed")
	}
	if _, err = store.Access(a, "gllive:missing", "key1_01", nil); err == nil {
		t.Fatal("unknown target accepted")
	}
}
