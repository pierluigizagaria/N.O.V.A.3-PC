package native

import (
	"database/sql"
	"nova3.test/backend/internal/match"
	"nova3.test/backend/internal/rooms"
)

func (s *Services) matchLoadouts(players []rooms.Player) (map[string]match.LoadoutPolicy, error) {
	policies := make(map[string]match.LoadoutPolicy, len(players))
	err := s.Store.Write(func(tx *sql.Tx) error {
		for _, player := range players {
			if err := s.Shop.CheckProfile(tx, player.Account, s.Shop.Catalogue.Version); err != nil {
				return err
			}
			inventory, err := s.Shop.Inventory(tx, player.Account)
			if err != nil {
				return err
			}
			var policy match.LoadoutPolicy
			groups := [...]string{"perks", "perks", "primary", "addons", "addons", "secondary", "addons", "addons", "items"}
			for field, group := range groups {
				policy[field][0] = group != "primary" && group != "secondary"
				for _, item := range s.Shop.Groups[group] {
					_, owned := inventory[item.ID]
					if owned && item.Supported && item.Native != nil && item.Native.Ordinal >= 0 && item.Native.Ordinal < 127 {
						policy[field][item.Native.Ordinal+1] = true
					}
				}
			}
			policies[player.Account] = policy
		}
		return nil
	})
	return policies, err
}
