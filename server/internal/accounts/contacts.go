package accounts

import (
	"database/sql"
	"errors"
	"strconv"
	"time"

	"nova3.test/backend/internal/persistence"
)

const MaxContacts = 100

type Contact struct {
	Account string
	Number  int64
	Name    string
	Country string
}

// FindPublic accepts exactly one native public selector. Session tokens and
// internal account IDs are deliberately not searchable social identifiers.
func FindPublic(s *persistence.Store, name, number string) (string, error) {
	if (name == "") == (number == "") {
		return "", errors.New("one public identity required")
	}
	var id string
	var err error
	if name != "" {
		err = s.DB.QueryRow("SELECT id FROM accounts WHERE name=?", name).Scan(&id)
	} else {
		n, e := strconv.ParseInt(number, 10, 32)
		if e != nil || n <= 0 {
			return "", errors.New("invalid public user number")
		}
		err = s.DB.QueryRow("SELECT account FROM lobby_identities WHERE number=?", n).Scan(&id)
	}
	return id, err
}

// AddContact is an owner-scoped address-book operation. The game calls add
// directly and has no observed native confirmation UI. Reciprocal consent is
// not inferred: this grants no access to another player's private data.
func AddContact(s *persistence.Store, owner, target string) error {
	if owner == target {
		return errors.New("cannot add self")
	}
	if _, _, err := PublicIdentity(s, target); err != nil {
		return err
	}
	return s.Write(func(tx *sql.Tx) error {
		var exists, count int
		if err := tx.QueryRow("SELECT count(*) FROM contacts WHERE owner=? AND target=?", owner, target).Scan(&exists); err != nil {
			return err
		}
		if exists != 0 {
			return nil
		}
		if err := tx.QueryRow("SELECT count(*) FROM contacts WHERE owner=?", owner).Scan(&count); err != nil {
			return err
		}
		if count >= MaxContacts {
			return errors.New("contact limit reached")
		}
		_, err := tx.Exec("INSERT INTO contacts(owner,target,created) VALUES(?,?,?)", owner, target, time.Now().Unix())
		return err
	})
}

func DeleteContact(s *persistence.Store, owner, target string) error {
	_, err := s.DB.Exec("DELETE FROM contacts WHERE owner=? AND target=?", owner, target)
	return err
}

// Confirmation acts only on a contact another account already created. The
// recipient's authenticated consent is required before making it reciprocal.
func ConfirmContact(s *persistence.Store, owner, target string, accept bool) error {
	return s.Write(func(tx *sql.Tx) error {
		var exists int
		if err := tx.QueryRow("SELECT count(*) FROM contacts WHERE owner=? AND target=?", target, owner).Scan(&exists); err != nil {
			return err
		}
		if exists == 0 {
			return errors.New("no incoming contact")
		}
		if !accept {
			_, err := tx.Exec("DELETE FROM contacts WHERE owner=? AND target=?", target, owner)
			return err
		}
		var count, current int
		if err := tx.QueryRow("SELECT count(*) FROM contacts WHERE owner=?", owner).Scan(&count); err != nil {
			return err
		}
		if err := tx.QueryRow("SELECT count(*) FROM contacts WHERE owner=? AND target=?", owner, target).Scan(&current); err != nil {
			return err
		}
		if current == 0 && count >= MaxContacts {
			return errors.New("contact limit reached")
		}
		_, err := tx.Exec("INSERT INTO contacts(owner,target,created) VALUES(?,?,?) ON CONFLICT(owner,target) DO NOTHING", owner, target, time.Now().Unix())
		return err
	})
}

func Contacts(s *persistence.Store, owner string, start, count int) ([]Contact, error) {
	if start < 0 || start > MaxContacts || count < 0 || count > MaxContacts {
		return nil, errors.New("invalid contacts page")
	}
	if count == 0 {
		count = MaxContacts
	}
	rows, err := s.DB.Query(`SELECT a.id,i.number,a.name,COALESCE(p.country,'') FROM contacts c JOIN accounts a ON a.id=c.target
JOIN lobby_identities i ON i.account=a.id LEFT JOIN account_information p ON p.account=a.id WHERE c.owner=? ORDER BY a.name,a.id LIMIT ? OFFSET ?`, owner, count, start)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var result []Contact
	for rows.Next() {
		var c Contact
		if err := rows.Scan(&c.Account, &c.Number, &c.Name, &c.Country); err != nil {
			return nil, err
		}
		if !informationText(c.Country) {
			return nil, errors.New("invalid contact country")
		}
		result = append(result, c)
	}
	return result, rows.Err()
}
