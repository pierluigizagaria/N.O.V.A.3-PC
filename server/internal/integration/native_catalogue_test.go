package integration_test

import (
	"strings"
	"testing"

	"nova3.test/backend/internal/catalogue"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/loadout"
)

func TestNativeCoordinatesAndBonusSupplySeparation(t *testing.T) {
	s, _ := setup(t)
	// Invented coordinates: structural/domain coverage, not original game data.
	c := catalogue.Catalogue{Version: "native-fixture-v1", Source: "native", LibrarySHA256: "a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e", ResourceSHA256: strings.Repeat("a", 64), Items: []catalogue.Item{
		{ID: "fixture.weapon", Kind: "weapon", Supported: true, Native: &catalogue.NativeRef{Group: "primary", Ordinal: 0, TypeID: 900, ResourceObjectID: 800}},
		{ID: "fixture.grenade", Kind: "grenade", Supported: true, Native: &catalogue.NativeRef{Group: "items", Ordinal: 0, TypeID: 901, ResourceObjectID: 801}},
		{ID: "fixture.bonus", Kind: "consumable", Supported: true, MaxQuantity: 20, Native: &catalogue.NativeRef{Group: "consumables", Ordinal: 0, TypeID: 902, ResourceObjectID: 802}},
	}}
	must(t, s.Admit(c))
	a := create(t, s, "native-tester")
	must(t, grants.Provision(s, a.ID, c.Version))
	must(t, loadout.Set(s, a.ID, loadout.Selection{"primary": "fixture.weapon", "grenade": "fixture.grenade"}))
	must(t, grants.ResetConsumables(s, a.ID, 0))
	selection, err := loadout.Get(s, a.ID)
	must(t, err)
	if selection["grenade"] != "fixture.grenade" {
		t.Fatal("bonus reset altered grenade equipment")
	}
	inventory, err := grants.Inventory(s, a.ID)
	must(t, err)
	for _, item := range inventory {
		if item.Quantity != 0 {
			t.Fatal("supply not reset")
		}
	}
	bad := c
	bad.Items = append([]catalogue.Item{}, c.Items...)
	bad.Items[1].MaxQuantity = 20
	rejected(t, bad.Validate())
	bad = c
	bad.Items = append([]catalogue.Item{}, c.Items...)
	ref := *bad.Items[0].Native
	ref.Ordinal = 1
	bad.Items[0].Native = &ref
	rejected(t, bad.Validate())
	bad = c
	extra := c.Items[0]
	extra.ID = "fixture.duplicate"
	bad.Items = append(append([]catalogue.Item{}, c.Items...), extra)
	rejected(t, bad.Validate())
	bad = c
	bad.ResourceSHA256 = ""
	rejected(t, bad.Validate())
}
