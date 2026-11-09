package accounts

import (
	"database/sql"
	"strconv"

	"nova3.test/backend/internal/persistence"
)

// LobbyIdentity is a stable, unique player identifier fitting the original
// lobby's 16-byte fields. Account IDs and storage credentials remain separate.
// A database-assigned integer avoids truncating long account IDs or names.
func LobbyIdentity(s *persistence.Store, account string) (id, name string, err error) {
	number, name, err := PublicIdentity(s, account)
	if err != nil {
		return "", "", err
	}
	if len(name) > 16 {
		name = name[:16]
	}
	return "p" + strconv.FormatInt(number, 36), name, nil
}

// PublicIdentity supplies the native user number without exposing account IDs
// or session credentials. It is shared by lobby, friends and rankings.
func PublicIdentity(s *persistence.Store, account string) (number int64, name string, err error) {
	err = s.Write(func(tx *sql.Tx) error {
		if e := tx.QueryRow("SELECT name FROM accounts WHERE id=?", account).Scan(&name); e != nil {
			return e
		}
		if _, e := tx.Exec("INSERT INTO lobby_identities(account) VALUES(?) ON CONFLICT(account) DO NOTHING", account); e != nil {
			return e
		}
		if e := tx.QueryRow("SELECT number FROM lobby_identities WHERE account=?", account).Scan(&number); e != nil {
			return e
		}
		return nil
	})
	return
}
