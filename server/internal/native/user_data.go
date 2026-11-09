package native

import (
	"database/sql"
	"errors"
	"strings"
	"unicode/utf8"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/catalogue"
	"nova3.test/backend/internal/protocol/legacy"
)

const maxPublicUserKeys = 64

func publicDataValue(body string) bool {
	return len(body) <= 2048 && utf8.ValidString(body) && !strings.ContainsAny(body, "|\x00\r\n")
}

// The legacy SDK's user store supports explicit public-name/number reads.
// Keep it separate from private Seshat equipment and server-owned statistics.
// Arbitrary SDK values never grant inventory, scores or authority parameters.
func (s *Services) userData(r legacy.Request) (string, error) {
	key := r.Fields["k"]
	if len(key) > 63 || !catalogue.ValidID(key) {
		return "", errors.New("invalid public data key")
	}
	account := r.Fields["u"]
	if r.Fields["n"] != "" || r.Fields["un"] != "" {
		if r.Function != 65 {
			return "", errors.New("public data owner required")
		}
		var err error
		account, err = accounts.FindPublic(s.Store, r.Fields["n"], r.Fields["un"])
		if err != nil {
			return "", err
		}
	}
	if r.Function == 72 {
		_, err := s.Store.DB.Exec("DELETE FROM public_user_data WHERE account=? AND key=?", account, key)
		return "", err
	}
	if r.Function == 66 {
		body, exists := r.Fields["d"]
		if !exists || !publicDataValue(body) {
			return "", errors.New("invalid public data value")
		}
		err := s.Store.Write(func(tx *sql.Tx) error {
			var count, exists int
			if err := tx.QueryRow("SELECT count(*),COALESCE(sum(key=?),0) FROM public_user_data WHERE account=?", key, account).Scan(&count, &exists); err != nil {
				return err
			}
			if count >= maxPublicUserKeys && exists == 0 {
				return errors.New("public data quota reached")
			}
			_, err := tx.Exec("INSERT INTO public_user_data(account,key,body) VALUES(?,?,?) ON CONFLICT(account,key) DO UPDATE SET body=excluded.body", account, key, body)
			return err
		})
		return "", err
	}
	var body string
	err := s.Store.DB.QueryRow("SELECT body FROM public_user_data WHERE account=? AND key=?", account, key).Scan(&body)
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return "", err
	}
	if !publicDataValue(body) {
		return "", errors.New("invalid stored public data")
	}
	_, name, err := accounts.PublicIdentity(s.Store, account)
	if err != nil {
		return "", err
	}
	// Always use the original optional identity prefix: a literal value "n"
	// must not be mistaken by processUserData for the prefix itself.
	return "n|" + informationLabel(name) + "|" + body + "|", nil
}
