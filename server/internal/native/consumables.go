package native

import "database/sql"

// Charge writes are authoritative match operations. A native profile save is
// an equipment/preferences update and cannot refill or spend these quantities.
func (s *Services) consumeConsumable(account string, kind byte) (spent bool, err error) {
	if s.Shop == nil || int(kind) >= len(s.Shop.Groups["consumables"]) {
		return false, nil
	}
	item := s.Shop.Groups["consumables"][kind]
	if !item.Supported {
		return false, nil
	}
	err = s.Store.Write(func(tx *sql.Tx) error {
		result, e := tx.Exec("UPDATE entitlements SET quantity=quantity-1 WHERE account=? AND item=? AND quantity>0", account, item.ID)
		if e != nil {
			return e
		}
		count, e := result.RowsAffected()
		spent = count == 1
		return e
	})
	if err == nil && spent && s.Log != nil {
		s.Log.Printf("native consumable spent account=%s type=%d", account, kind)
	}
	return
}
