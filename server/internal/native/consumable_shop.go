package native

import (
	"database/sql"
	"encoding/hex"
	"errors"
	"io"
	"net/http"
	"strconv"
	"strings"

	"nova3.test/backend/internal/accounts"
)

// Explicit purchase intents are distinct from stale native profile saves.
// A durable nonce makes an uncertain network retry safe, including rejections.
func (s *Services) consumableInventory(w http.ResponseWriter, r *http.Request) {
	if (r.TLS == nil && !s.ProxyTLS) || (r.Method != "GET" && r.Method != "POST") {
		reply(w, 400, "unsupported inventory request")
		return
	}
	token, ok := strings.CutPrefix(r.Header.Get("Authorization"), "Bearer ")
	if !ok || token == "" {
		reply(w, 401, "session required")
		return
	}
	nonce, kind := "", -1
	if r.Method == "POST" {
		body, err := io.ReadAll(io.LimitReader(r.Body, 36))
		if err != nil || len(body) != 34 || body[32] != '|' || body[33] < '0' || body[33] > '8' {
			reply(w, 400, "invalid purchase")
			return
		}
		nonce, kind = string(body[:32]), int(body[33]-'0')
		if _, err = hex.DecodeString(nonce); err != nil || strings.ToLower(nonce) != nonce {
			reply(w, 400, "invalid purchase nonce")
			return
		}
	}
	var result string
	unauthorized := false
	err := s.Store.Write(func(tx *sql.Tx) error {
		account, err := accounts.ResolveWith(tx, token)
		if err != nil {
			unauthorized = true
			return err
		}
		if err = s.Shop.CheckProfile(tx, account, s.Shop.Catalogue.Version); err != nil {
			return err
		}
		accepted := 0
		if kind >= 0 {
			var oldKind int
			err = tx.QueryRow("SELECT kind,accepted FROM consumable_purchases WHERE account=? AND nonce=?", account, nonce).Scan(&oldKind, &accepted)
			if err == nil && oldKind != kind {
				return errors.New("purchase nonce reused")
			}
			if errors.Is(err, sql.ErrNoRows) {
				item := s.Shop.Groups["consumables"][kind]
				price := s.Shop.Rows[item.ID].ResourcePrice
				var quantity int
				if err = tx.QueryRow("SELECT quantity FROM entitlements WHERE account=? AND item=?", account, item.ID).Scan(&quantity); err != nil {
					return err
				}
				// Native counts are signed integers; the initial grant of20 is
				// not an inventory capacity or a promise of unlimited refills.
				if item.Supported && quantity < 2147483647 && price > 0 {
					debit, e := tx.Exec("UPDATE wallets SET coins=coins-? WHERE account=? AND coins>=?", price, account, price)
					if e != nil {
						return e
					}
					rows, e := debit.RowsAffected()
					if e != nil {
						return e
					}
					if rows == 1 {
						if _, e = tx.Exec("UPDATE entitlements SET quantity=quantity+1 WHERE account=? AND item=?", account, item.ID); e != nil {
							return e
						}
						accepted = 1
					}
				}
				if _, err = tx.Exec("INSERT INTO consumable_purchases(account,nonce,kind,accepted) VALUES(?,?,?,?)", account, nonce, kind, accepted); err != nil {
					return err
				}
			} else if err != nil {
				return err
			}
		}
		var coins int
		if err = tx.QueryRow("SELECT coins FROM wallets WHERE account=?", account).Scan(&coins); err != nil {
			return err
		}
		fields := []string{strconv.Itoa(accepted), strconv.Itoa(coins)}
		for _, item := range s.Shop.Groups["consumables"] {
			var quantity int
			if err = tx.QueryRow("SELECT quantity FROM entitlements WHERE account=? AND item=?", account, item.ID).Scan(&quantity); err != nil {
				return err
			}
			fields = append(fields, strconv.Itoa(quantity))
		}
		result = strings.Join(fields, "|")
		return nil
	})
	if unauthorized {
		reply(w, 401, "invalid session")
		return
	}
	if err != nil {
		reply(w, 400, "inventory request rejected")
		return
	}
	reply(w, 200, result)
}
