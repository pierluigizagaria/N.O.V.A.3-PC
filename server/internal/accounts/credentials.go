package accounts

import (
	"crypto/pbkdf2"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"database/sql"
	"errors"
	"strings"

	"nova3.test/backend/internal/catalogue"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/persistence"
)

const passwordIterations = 210000

// SetPassword is trusted local administration. Passwords never appear in arguments or logs.
func SetPassword(s *persistence.Store, account, password string) error {
	salt, derived, err := derivePassword(password)
	if err != nil {
		return err
	}
	var name string
	if err := s.DB.QueryRow("SELECT name FROM accounts WHERE id=?", account).Scan(&name); err != nil {
		return err
	}
	if len(name) > 32 {
		return errors.New("account name exceeds 32 bytes")
	}
	_, err = s.DB.Exec("INSERT INTO credentials(account,salt,derived) VALUES(?,?,?) ON CONFLICT(account) DO UPDATE SET salt=excluded.salt,derived=excluded.derived", account, salt, derived)
	return err
}

func derivePassword(password string) ([]byte, []byte, error) {
	if len(password) < 8 || len(password) > 64 || strings.ContainsAny(password, "\x00|\r\n") {
		return nil, nil, errors.New("password must contain 8..64 bytes without native delimiters")
	}
	salt := make([]byte, 16)
	if _, err := rand.Read(salt); err != nil {
		return nil, nil, err
	}
	derived, err := pbkdf2.Key(sha256.New, password, salt, passwordIterations, 32)
	return salt, derived, err
}

// Register uses the same catalogue grant policy as locally created accounts.
// No partially provisioned identity is visible if any step fails.
func Register(s *persistence.Store, name, password, version string) (Account, error) {
	return RegisterWithInformation(s, name, password, version, nil)
}

func RegisterWithInformation(s *persistence.Store, name, password, version string, fields map[string]string) (Account, error) {
	var columns []string
	var values []any
	if len(fields) != 0 {
		var err error
		columns, values, err = informationUpdate(fields)
		if err != nil {
			return Account{}, err
		}
	}
	if !catalogue.ValidID(name) || len(name) > 16 {
		return Account{}, errors.New("invalid account name")
	}
	salt, derived, err := derivePassword(password)
	if err != nil {
		return Account{}, err
	}
	id, err := randomToken()
	if err != nil {
		return Account{}, err
	}
	a := Account{ID: id, Name: name}
	err = s.Write(func(tx *sql.Tx) error {
		if _, err := tx.Exec("INSERT INTO accounts(id,name) VALUES(?,?)", id, name); err != nil {
			return err
		}
		if _, err := tx.Exec("INSERT INTO credentials(account,salt,derived) VALUES(?,?,?)", id, salt, derived); err != nil {
			return err
		}
		if err := grants.ProvisionInTransaction(tx, id, version); err != nil {
			return err
		}
		if len(columns) != 0 {
			return patchInformation(tx, id, columns, values)
		}
		return nil
	})
	if err != nil {
		return Account{}, err
	}
	return a, nil
}

// ChangePassword requires proof of the old password in addition to a session.
// Rechecking the stored hash under the write transaction prevents two racing
// changes from both succeeding. Existing sessions are revoked atomically.
func ChangePassword(s *persistence.Store, account, old, next string) error {
	if len(old) > 64 {
		return errors.New("invalid credentials")
	}
	var oldSalt, expected []byte
	if err := s.DB.QueryRow("SELECT salt,derived FROM credentials WHERE account=?", account).Scan(&oldSalt, &expected); err != nil {
		return err
	}
	actual, err := pbkdf2.Key(sha256.New, old, oldSalt, passwordIterations, 32)
	if err != nil || subtle.ConstantTimeCompare(actual, expected) != 1 {
		return errors.New("invalid credentials")
	}
	salt, derived, err := derivePassword(next)
	if err != nil {
		return err
	}
	return s.Write(func(tx *sql.Tx) error {
		result, err := tx.Exec("UPDATE credentials SET salt=?,derived=? WHERE account=? AND salt=? AND derived=?", salt, derived, account, oldSalt, expected)
		if err != nil {
			return err
		}
		if n, _ := result.RowsAffected(); n != 1 {
			return errors.New("credentials changed concurrently")
		}
		_, err = tx.Exec("DELETE FROM sessions WHERE account=?", account)
		return err
	})
}

// ChangeName keeps every foreign-key identity and requires fresh login so active
// lobby rosters cannot retain a different display name from the account store.
func ChangeName(s *persistence.Store, account, password, next string) error {
	if !catalogue.ValidID(next) || len(next) > 16 || len(password) > 64 {
		return errors.New("invalid account name or credentials")
	}
	var salt, expected []byte
	var previous string
	if err := s.DB.QueryRow("SELECT a.name,c.salt,c.derived FROM accounts a JOIN credentials c ON c.account=a.id WHERE a.id=?", account).Scan(&previous, &salt, &expected); err != nil {
		return err
	}
	actual, err := pbkdf2.Key(sha256.New, password, salt, passwordIterations, 32)
	if err != nil || subtle.ConstantTimeCompare(actual, expected) != 1 {
		return errors.New("invalid credentials")
	}
	return s.Write(func(tx *sql.Tx) error {
		result, err := tx.Exec("UPDATE accounts SET name=? WHERE id=? AND name=? AND EXISTS(SELECT 1 FROM credentials WHERE account=? AND salt=? AND derived=?)", next, account, previous, account, salt, expected)
		if err != nil {
			return err
		}
		if n, _ := result.RowsAffected(); n != 1 {
			return errors.New("account changed concurrently")
		}
		_, err = tx.Exec("DELETE FROM sessions WHERE account=?", account)
		return err
	})
}

// Authenticate returns the persistent account identity, not a client-supplied UID.
func Authenticate(s *persistence.Store, name, password string) (string, error) {
	if len(name) == 0 || len(name) > 32 || len(password) > 64 {
		return "", errors.New("invalid credentials")
	}
	var id string
	var salt, expected []byte
	err := s.DB.QueryRow("SELECT a.id,c.salt,c.derived FROM accounts a JOIN credentials c ON c.account=a.id WHERE a.name=?", name).Scan(&id, &salt, &expected)
	if err != nil {
		// Missing accounts still take the password derivation path.
		salt, expected = make([]byte, 16), make([]byte, 32)
	}
	actual, deriveErr := pbkdf2.Key(sha256.New, password, salt, passwordIterations, 32)
	if err != nil || deriveErr != nil || subtle.ConstantTimeCompare(actual, expected) != 1 {
		return "", errors.New("invalid credentials")
	}
	return id, nil
}
