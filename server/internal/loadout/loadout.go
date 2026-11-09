// Package loadout validates complete replacement loadouts against test grants.
package loadout

import (
	"database/sql"
	"encoding/json"
	"errors"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/persistence"
)

type Selection map[string]string

var kinds = map[string]string{"primary": "weapon", "secondary": "weapon", "grenade": "grenade", "perk": "perk"}

func SetForSession(s *persistence.Store, token string, selection Selection) error {
	return s.Write(func(tx *sql.Tx) error {
		id, err := accounts.ResolveWith(tx, token)
		if err != nil {
			return err
		}
		return set(tx, id, selection)
	})
}
func GetForSession(s *persistence.Store, token string) (Selection, error) {
	id, err := accounts.Resolve(s, token)
	if err != nil {
		return nil, err
	}
	return Get(s, id)
}
func Set(s *persistence.Store, account string, selection Selection) error {
	return s.Write(func(tx *sql.Tx) error { return set(tx, account, selection) })
}
func set(tx *sql.Tx, account string, selection Selection) error {
	if len(selection) == 0 || len(selection) > len(kinds) {
		return errors.New("loadout must contain 1..4 known slots")
	}
	var version string
	if err := tx.QueryRow("SELECT version FROM profiles WHERE account=?", account).Scan(&version); err != nil {
		return err
	}
	c, err := persistence.Catalogue(tx, version)
	if err != nil {
		return err
	}
	for slot, id := range selection {
		kind, ok := kinds[slot]
		if !ok {
			return errors.New("unknown equipment slot")
		}
		item, ok := c.Find(id)
		if !ok || !item.Supported || item.Kind != kind {
			return errors.New("unknown, unsupported or incompatible item")
		}
		var quantity int
		if err = tx.QueryRow("SELECT quantity FROM entitlements WHERE account=? AND item=?", account, id).Scan(&quantity); err != nil {
			return errors.New("item is not owned")
		}
		if item.HasSupply() && quantity == 0 {
			return errors.New("consumable has no test supply")
		}
	}
	body, err := json.Marshal(selection)
	if err != nil {
		return err
	}
	_, err = tx.Exec("INSERT INTO loadouts(account,body) VALUES(?,?) ON CONFLICT(account) DO UPDATE SET body=excluded.body", account, string(body))
	if err == nil {
		_, err = tx.Exec("DELETE FROM native_equipment WHERE account=?", account)
	}
	return err
}

// SetInTransaction is used by account-bound compatibility storage updates.
func SetInTransaction(tx *sql.Tx, account string, selection Selection) error {
	return set(tx, account, selection)
}
func Get(s *persistence.Store, account string) (Selection, error) {
	var exists int
	if err := s.DB.QueryRow("SELECT 1 FROM profiles WHERE account=?", account).Scan(&exists); err != nil {
		return nil, err
	}
	var body string
	err := s.DB.QueryRow("SELECT body FROM loadouts WHERE account=?", account).Scan(&body)
	if errors.Is(err, sql.ErrNoRows) {
		return Selection{}, nil
	}
	if err != nil {
		return nil, err
	}
	var out Selection
	err = json.Unmarshal([]byte(body), &out)
	return out, err
}
