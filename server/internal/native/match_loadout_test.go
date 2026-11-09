package native

import (
	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/rooms"
	"testing"
)

func TestMatchLoadoutsFreezeOwnedCatalogueSelections(t *testing.T) {
	s, account, _, _ := fixture(t)
	players := []rooms.Player{{Account: account}}
	policies, err := s.matchLoadouts(players)
	if err != nil {
		t.Fatal(err)
	}
	v := game.Loadout{Selections: [9]byte{1, 2, 1, 1, 2, 2, 1, 2, 1}, Jetpack: true}
	if !policies[account].Allows(v) {
		t.Fatal("owned one-based equipment denied")
	}
	item := s.Shop.Groups["addons"][1].ID
	if _, err := s.Store.DB.Exec("DELETE FROM entitlements WHERE account=? AND item=?", account, item); err != nil {
		t.Fatal(err)
	}
	updated, err := s.matchLoadouts(players)
	if err != nil || updated[account].Allows(v) || !policies[account].Allows(v) {
		t.Fatal("ownership not frozen per allocation", err)
	}
	v.Selections = [9]byte{2: 1, 5: 1}
	if !updated[account].Allows(v) {
		t.Fatal("empty optional selections denied")
	}
	for _, field := range []int{2, 5} {
		bad := v
		bad.Selections[field] = 0
		if updated[account].Allows(bad) {
			t.Fatal("empty required weapon admitted")
		}
	}
	for field := range v.Selections {
		bad := v
		bad.Selections[field] = 127
		if updated[account].Allows(bad) {
			t.Fatal("unknown selector admitted")
		}
	}
	if _, err := s.matchLoadouts([]rooms.Player{{Account: "missing"}}); err == nil {
		t.Fatal("unprovisioned account admitted")
	}
}
