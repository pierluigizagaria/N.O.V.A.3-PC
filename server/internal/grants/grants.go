// Package grants implements the all-unlocked test policy, independently of economy.
package grants

import (
	"database/sql"
	"encoding/json"
	"errors"

	"nova3.test/backend/internal/persistence"
)

type Entitlement struct {
	Item     string `json:"item"`
	Quantity int    `json:"quantity"`
}

func Provision(s *persistence.Store, account, version string) error {
	return s.Write(func(tx *sql.Tx) error {
		c, err := persistence.Catalogue(tx, version)
		if err != nil {
			return err
		}
		var current string
		err = tx.QueryRow("SELECT version FROM profiles WHERE account=?", account).Scan(&current)
		if err == nil && current != version {
			return errors.New("account is already bound to another catalogue")
		}
		if err != nil && !errors.Is(err, sql.ErrNoRows) {
			return err
		}
		if _, err = tx.Exec("INSERT INTO profiles(account,version) VALUES(?,?) ON CONFLICT(account) DO NOTHING", account, version); err != nil {
			return err
		}
		for _, item := range c.Items {
			if !item.Supported {
				continue
			}
			quantity := item.MaxQuantity
			if _, err = tx.Exec("INSERT INTO entitlements(account,item,quantity) VALUES(?,?,?) ON CONFLICT(account,item) DO NOTHING", account, item.ID, quantity); err != nil {
				return err
			}
		}
		return nil
	})
}
func ResetConsumables(s *persistence.Store, account string, quantity int) error {
	if quantity < 0 || quantity > 1000 {
		return errors.New("quantity must be 0..1000")
	}
	return s.Write(func(tx *sql.Tx) error {
		var version string
		if err := tx.QueryRow("SELECT version FROM profiles WHERE account=?", account).Scan(&version); err != nil {
			return err
		}
		c, err := persistence.Catalogue(tx, version)
		if err != nil {
			return err
		}
		// Validate every cap before applying any update.
		for _, item := range c.Items {
			if item.Supported && item.Kind == "grenade" && quantity > item.MaxQuantity {
				return errors.New("quantity exceeds an item cap")
			}
		}
		for _, item := range c.Items {
			if item.Supported && item.Kind == "grenade" {
				if _, err = tx.Exec("UPDATE entitlements SET quantity=? WHERE account=? AND item=?", quantity, account, item.ID); err != nil {
					return err
				}
			}
		}
		if quantity == 0 {
			var body string
			err = tx.QueryRow("SELECT body FROM loadouts WHERE account=?", account).Scan(&body)
			if err != nil && !errors.Is(err, sql.ErrNoRows) {
				return err
			}
			if err == nil {
				var selection map[string]string
				if err = json.Unmarshal([]byte(body), &selection); err != nil {
					return err
				}
				delete(selection, "grenade")
				data, e := json.Marshal(selection)
				if e != nil {
					return e
				}
				if _, err = tx.Exec("UPDATE loadouts SET body=? WHERE account=?", string(data), account); err != nil {
					return err
				}
			}
		}
		return nil
	})
}
func Inventory(s *persistence.Store, account string) ([]Entitlement, error) {
	var exists int
	if err := s.DB.QueryRow("SELECT 1 FROM profiles WHERE account=?", account).Scan(&exists); err != nil {
		return nil, err
	}
	rows, err := s.DB.Query("SELECT item,quantity FROM entitlements WHERE account=? ORDER BY item", account)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := []Entitlement{}
	for rows.Next() {
		var e Entitlement
		if err = rows.Scan(&e.Item, &e.Quantity); err != nil {
			return nil, err
		}
		out = append(out, e)
	}
	return out, rows.Err()
}
