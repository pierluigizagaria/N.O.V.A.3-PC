package integration_test

import (
	"encoding/json"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"sync"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/catalogue"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/loadout"
	"nova3.test/backend/internal/persistence"
)

func must(t *testing.T, err error) {
	t.Helper()
	if err != nil {
		t.Fatal(err)
	}
}
func rejected(t *testing.T, err error) {
	t.Helper()
	if err == nil {
		t.Fatal("expected rejection")
	}
}
func setup(t *testing.T) (*persistence.Store, catalogue.Catalogue) {
	t.Helper()
	data, err := os.ReadFile("../../testdata/synthetic-catalogue.json")
	must(t, err)
	c, err := catalogue.Decode(data)
	must(t, err)
	s, err := persistence.Open(filepath.Join(t.TempDir(), "test.db"))
	must(t, err)
	t.Cleanup(func() { s.Close() })
	must(t, s.Admit(c))
	return s, c
}
func create(t *testing.T, s *persistence.Store, name string) accounts.Account {
	t.Helper()
	a, err := accounts.Create(s, name)
	must(t, err)
	return a
}
func TestProvisionResetAndDurableLoadout(t *testing.T) {
	s, c := setup(t)
	a := create(t, s, "alice")
	must(t, grants.Provision(s, a.ID, c.Version))
	selected := loadout.Selection{"primary": "test.rifle", "grenade": "test.grenade"}
	must(t, loadout.Set(s, a.ID, selected))
	must(t, grants.ResetConsumables(s, a.ID, 3))
	must(t, grants.Provision(s, a.ID, c.Version))
	inv, err := grants.Inventory(s, a.ID)
	must(t, err)
	if len(inv) != 4 {
		t.Fatal(inv)
	}
	for _, e := range inv {
		if e.Item == "test.grenade" && e.Quantity != 3 {
			t.Fatal("provision refilled consumable")
		}
	}
	var path string
	must(t, s.DB.QueryRow("SELECT file FROM pragma_database_list WHERE name='main'").Scan(&path))
	must(t, s.Close())
	reopened, err := persistence.Open(path)
	must(t, err)
	defer reopened.Close()
	actual, err := loadout.Get(reopened, a.ID)
	must(t, err)
	if !reflect.DeepEqual(actual, selected) {
		t.Fatal(actual)
	}
	inv, err = grants.Inventory(reopened, a.ID)
	must(t, err)
	for _, e := range inv {
		if e.Item == "test.grenade" && e.Quantity != 3 {
			t.Fatal("quantity lost across restart")
		}
	}
}
func TestAccountsSessionsAndIsolation(t *testing.T) {
	s, c := setup(t)
	a := create(t, s, "alice")
	b := create(t, s, "bob")
	must(t, grants.Provision(s, a.ID, c.Version))
	must(t, grants.Provision(s, b.ID, c.Version))
	token, err := accounts.Issue(s, a.ID, time.Hour)
	must(t, err)
	id, err := accounts.Resolve(s, token)
	must(t, err)
	if id != a.ID {
		t.Fatal(id)
	}
	must(t, loadout.SetForSession(s, token, loadout.Selection{"primary": "test.rifle"}))
	other, err := loadout.Get(s, b.ID)
	must(t, err)
	if len(other) != 0 {
		t.Fatal("cross-account write")
	}
	var stored string
	must(t, s.DB.QueryRow("SELECT hash FROM sessions").Scan(&stored))
	if stored == token {
		t.Fatal("plaintext token persisted")
	}
	must(t, accounts.Revoke(s, token))
	_, err = accounts.Resolve(s, token)
	rejected(t, err)
	rejected(t, loadout.SetForSession(s, token, loadout.Selection{"primary": "test.pistol"}))
	token, err = accounts.Issue(s, b.ID, time.Hour)
	must(t, err)
	_, err = s.DB.Exec("UPDATE sessions SET expires=0")
	must(t, err)
	_, err = accounts.Resolve(s, token)
	rejected(t, err)
	_, err = accounts.Issue(s, "missing", time.Hour)
	rejected(t, err)
	_, err = accounts.Issue(s, a.ID, 48*time.Hour)
	rejected(t, err)
	_, err = accounts.Create(s, "alice")
	rejected(t, err)
	_, err = accounts.Create(s, "bad name")
	rejected(t, err)
}
func TestInvalidLoadoutIsAtomicAndOwnershipRequired(t *testing.T) {
	s, c := setup(t)
	a := create(t, s, "alice")
	must(t, grants.Provision(s, a.ID, c.Version))
	base := loadout.Selection{"primary": "test.rifle"}
	must(t, loadout.Set(s, a.ID, base))
	for _, candidate := range []loadout.Selection{{"primary": "missing"}, {"primary": "test.unavailable"}, {"grenade": "test.rifle"}, {"fake": "test.rifle"}, {"primary": "test.pistol", "grenade": "missing"}, {}} {
		rejected(t, loadout.Set(s, a.ID, candidate))
		actual, err := loadout.Get(s, a.ID)
		must(t, err)
		if !reflect.DeepEqual(actual, base) {
			t.Fatal("invalid update altered loadout")
		}
	}
	_, err := s.DB.Exec("DELETE FROM entitlements WHERE account=? AND item='test.pistol'", a.ID)
	must(t, err)
	rejected(t, loadout.Set(s, a.ID, loadout.Selection{"secondary": "test.pistol"}))
	must(t, grants.ResetConsumables(s, a.ID, 0))
	rejected(t, loadout.Set(s, a.ID, loadout.Selection{"grenade": "test.grenade"}))
	rejected(t, grants.Provision(s, "missing", c.Version))
	_, err = grants.Inventory(s, "missing")
	rejected(t, err)
}
func TestCatalogueAdmissionAndVersionBinding(t *testing.T) {
	s, c := setup(t)
	must(t, s.Admit(c))
	changed := c
	changed.Items = append([]catalogue.Item{}, c.Items...)
	changed.Items[0].Supported = false
	rejected(t, s.Admit(changed))
	a := create(t, s, "alice")
	must(t, grants.Provision(s, a.ID, c.Version))
	changed.Version = "synthetic-v2"
	must(t, s.Admit(changed))
	rejected(t, grants.Provision(s, a.ID, changed.Version))
	for _, input := range []string{`{}`, `{"version":"v1","items":[],"source":"synthetic"}`, `{"version":"v1","source":"native","library_sha256":"wrong","items":[{"id":"x","kind":"weapon"}]}`, strings.Repeat(" ", 1024*1024+1)} {
		_, err := catalogue.Decode([]byte(input))
		rejected(t, err)
	}
	raw, err := json.Marshal(c)
	must(t, err)
	_, err = catalogue.Decode(append(raw, []byte(" {}")...))
	rejected(t, err)
	duplicate := c
	duplicate.Items = append(append([]catalogue.Item{}, c.Items...), c.Items[0])
	rejected(t, duplicate.Validate())
}
func TestResetBoundsAndSelectionCleanup(t *testing.T) {
	s, c := setup(t)
	a := create(t, s, "alice")
	must(t, grants.Provision(s, a.ID, c.Version))
	must(t, loadout.Set(s, a.ID, loadout.Selection{"grenade": "test.grenade", "primary": "test.rifle"}))
	for _, n := range []int{-1, 21, 1001} {
		rejected(t, grants.ResetConsumables(s, a.ID, n))
	}
	inv, err := grants.Inventory(s, a.ID)
	must(t, err)
	for _, e := range inv {
		if e.Item == "test.grenade" && e.Quantity != 20 {
			t.Fatal("invalid reset altered state")
		}
	}
	must(t, grants.ResetConsumables(s, a.ID, 0))
	selected, err := loadout.Get(s, a.ID)
	must(t, err)
	if len(selected) != 1 || selected["primary"] != "test.rifle" {
		t.Fatal(selected)
	}
}
func TestConcurrentProvisionAndTransactionRollback(t *testing.T) {
	s, c := setup(t)
	a := create(t, s, "alice")
	var wg sync.WaitGroup
	errs := make(chan error, 12)
	for i := 0; i < 12; i++ {
		wg.Add(1)
		go func() { defer wg.Done(); errs <- grants.Provision(s, a.ID, c.Version) }()
	}
	wg.Wait()
	close(errs)
	for err := range errs {
		must(t, err)
	}
	inv, err := grants.Inventory(s, a.ID)
	must(t, err)
	if len(inv) != 4 {
		t.Fatal(inv)
	}
	// Simulate a mid-write storage failure: no partial provisioning may survive.
	b := create(t, s, "bob")
	_, err = s.DB.Exec("CREATE TRIGGER deny_grant BEFORE INSERT ON entitlements WHEN NEW.item='test.pistol' BEGIN SELECT RAISE(ABORT,'injected failure'); END")
	must(t, err)
	rejected(t, grants.Provision(s, b.ID, c.Version))
	var count int
	must(t, s.DB.QueryRow("SELECT count(*) FROM profiles WHERE account=?", b.ID).Scan(&count))
	if count != 0 {
		t.Fatal("profile survived failed grant transaction")
	}
	must(t, s.DB.QueryRow("SELECT count(*) FROM entitlements WHERE account=?", b.ID).Scan(&count))
	if count != 0 {
		t.Fatal("partial grants survived")
	}
}
func TestRejectFutureSchema(t *testing.T) {
	path := filepath.Join(t.TempDir(), "future.db")
	s, err := persistence.Open(path)
	must(t, err)
	_, err = s.DB.Exec("PRAGMA user_version=99")
	must(t, err)
	must(t, s.Close())
	s, err = persistence.Open(path)
	rejected(t, err)
	if s != nil {
		t.Fatal("opened future schema")
	}
}
