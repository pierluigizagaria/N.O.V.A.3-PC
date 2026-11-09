package native

import (
	"database/sql"
	"errors"
	"strconv"
	"strings"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/adapters/shop"
	"nova3.test/backend/internal/persistence"
)

type Storage struct {
	DB   *persistence.Store
	Shop *shop.Adapter
}

func (s Storage) Access(token, identity, key string, update *string) (string, error) {
	var result string
	err := s.DB.Write(func(tx *sql.Tx) error {
		account, err := accounts.ResolveWith(tx, token)
		if err != nil {
			return err
		}
		// The original Seshat connection reuses its token across sequential
		// accesses. Keep active storage sessions alive without reviving expiry.
		if err = accounts.RefreshWith(tx, token, time.Hour); err != nil {
			return err
		}
		if err = s.Shop.CheckProfile(tx, account, s.Shop.Catalogue.Version); err != nil {
			return err
		}
		var name string
		if err = tx.QueryRow("SELECT name FROM accounts WHERE id=?", account).Scan(&name); err != nil {
			return err
		}
		if identity != "me" && identity != "gllive:"+name {
			// The original player-details screen requests these two public
			// records by name. Equipment/purchases and every write remain private.
			if update != nil || (key != "key1_01" && key != "key3_01") || !strings.HasPrefix(identity, "gllive:") {
				return errors.New("storage identity does not match session")
			}
			if err = tx.QueryRow("SELECT id FROM accounts WHERE name=?", strings.TrimPrefix(identity, "gllive:")).Scan(&account); err != nil {
				return err
			}
			if err = s.Shop.CheckProfile(tx, account, s.Shop.Catalogue.Version); err != nil {
				return err
			}
		}
		if key == "key2_01" {
			if update != nil {
				if err = s.Shop.Write(tx, account, *update); err != nil {
					return err
				}
			}
			result, err = s.Shop.Read(tx, account)
			return err
		}
		if key != "key1_01" && key != "key3_01" {
			return errors.New("unsupported native storage key")
		}
		if update != nil {
			if err = validateData(key, *update); err != nil {
				return err
			}
			if _, err = tx.Exec("INSERT INTO native_data(account,key,body) VALUES(?,?,?) ON CONFLICT(account,key) DO UPDATE SET body=excluded.body", account, key, *update); err != nil {
				return err
			}
		}
		err = tx.QueryRow("SELECT body FROM native_data WHERE account=? AND key=?", account, key).Scan(&result)
		if errors.Is(err, sql.ErrNoRows) {
			if key == "key3_01" {
				// Original Sniffer format: 70 counters and 96 achievement bits.
				result = strings.Repeat("0|", 70) + strings.Repeat("0", 96)
			} else {
				result = "0|0|0|0|0||" + strings.TrimSuffix(strings.Repeat("0^", 8), "^") + "||" + strings.TrimSuffix(strings.Repeat("0^", 16), "^")
			}
			return nil
		}
		if err == nil {
			err = validateData(key, result)
		}
		return err
	})
	return result, err
}

func validateData(key, text string) error {
	if len(text) == 0 || len(text) > 8192 || strings.ContainsAny(text, "\x00\r\n") {
		return errors.New("invalid native record size")
	}
	parts := strings.Split(text, "|")
	if key == "key3_01" {
		if len(parts) != 71 || len(parts[70]) != 96 || strings.Trim(parts[70], "01") != "" {
			return errors.New("invalid achievement record")
		}
		parts = parts[:70]
	} else if len(parts) != 9 {
		return errors.New("invalid player record")
	}
	for index, part := range parts {
		if len(part) > 511 {
			return errors.New("native field exceeds consumer capacity")
		}
		for _, value := range strings.Split(part, "^") {
			if value == "" && key == "key1_01" && (index == 5 || index == 7) && part == "" {
				continue
			}
			if n, err := strconv.ParseInt(value, 10, 32); err != nil || n < 0 || strconv.FormatInt(n, 10) != value {
				return errors.New("invalid native counter")
			} else if key == "key1_01" && index < 2 && n > 2147483647/60 {
				return errors.New("native date conversion overflows")
			}
		}
		if key == "key3_01" || index < 5 {
			if strings.Contains(part, "^") {
				return errors.New("array in scalar native field")
			}
		} else if index == 6 || index == 8 {
			count := 8
			if index == 8 {
				count = 16
			}
			if len(strings.Split(part, "^")) != count {
				return errors.New("invalid native fixed counter array")
			}
		}
	}
	return nil
}
