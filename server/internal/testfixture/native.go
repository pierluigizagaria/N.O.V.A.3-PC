// Package testfixture supplies invented catalogues shared by protocol tests.
package testfixture

import (
	"fmt"
	"nova3.test/backend/internal/adapters/shop"
	"nova3.test/backend/internal/catalogue"
	"strings"
)

func NativeCatalogue() (catalogue.Catalogue, shop.Mapping) {
	c := catalogue.Catalogue{Version: "wire-fixture-v1", Source: "native", LibrarySHA256: "a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e", ResourceSHA256: strings.Repeat("a", 64)}
	m := shop.Mapping{Library: c.LibrarySHA256, Resource: c.ResourceSHA256, Policy: shop.Policy{XP: 100, BonusQuantity: 20, BonusTestPolicy: true}}
	kinds := map[string]string{"primary": "weapon", "secondary": "weapon", "addons": "addon", "perks": "perk", "items": "equipment", "killchains": "killchain", "signatures": "signature", "presets": "preset", "consumables": "consumable"}
	groups := append(append([]string{}, shop.OwnershipGroups...), "consumables")
	for group, g := range groups {
		count := 2
		if g == "presets" {
			count = 4
		}
		if g == "signatures" {
			count = 3
		}
		if g == "consumables" {
			count = 9
		}
		for n := 0; n < count; n++ {
			kind := kinds[g]
			if g == "items" && n == 1 {
				kind = "grenade"
			}
			item := catalogue.Item{ID: fmt.Sprintf("fixture.%s.%d", g, n), Kind: kind, Supported: true, Native: &catalogue.NativeRef{Group: g, Ordinal: n, TypeID: 100 + n, ResourceObjectID: 800 + group}}
			if kind == "consumable" {
				item.MaxQuantity = 20
			}
			c.Items = append(c.Items, item)
			level := 1
			price := 10 + n
			if g == "signatures" && n > 1 {
				price = 11
			}
			m.Rows = append(m.Rows, shop.Row{ServerID: item.ID, Native: *item.Native, MinimumLevel: &level, ResourcePrice: price})
		}
	}
	return c, m
}
