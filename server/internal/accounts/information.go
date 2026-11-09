package accounts

import (
	"database/sql"
	"errors"
	"net/mail"
	"strings"
	"unicode/utf8"

	"nova3.test/backend/internal/persistence"
)

// Information is owner-editable profile data. It never contains a password,
// session credential, gameplay score or authority parameter.
type Information struct {
	Email, Country, FirstName, LastName, Birthday string
	EmailFlag, Sex                                int
}

func informationText(value string) bool {
	return len(value) <= 127 && utf8.ValidString(value) && !strings.ContainsAny(value, "|^\x00\r\n")
}

func ReadInformation(s *persistence.Store, account string) (Information, error) {
	var info Information
	err := s.DB.QueryRow(`SELECT COALESCE(i.email,''),COALESCE(i.email_flag,0),COALESCE(i.country,''),COALESCE(i.firstname,''),COALESCE(i.lastname,''),COALESCE(i.sex,0),COALESCE(i.birthday,'') FROM accounts a LEFT JOIN account_information i ON i.account=a.id WHERE a.id=?`, account).
		Scan(&info.Email, &info.EmailFlag, &info.Country, &info.FirstName, &info.LastName, &info.Sex, &info.Birthday)
	if err == nil {
		for _, value := range []string{info.Email, info.Country, info.FirstName, info.LastName, info.Birthday} {
			if !informationText(value) {
				return Information{}, errors.New("invalid stored profile text")
			}
		}
		if info.EmailFlag < 0 || info.EmailFlag > 1 || info.Sex < 0 || info.Sex > 1 {
			return Information{}, errors.New("invalid stored profile flag")
		}
	}
	return info, err
}

// PatchInformation updates only supplied fields, atomically. The whitelist is
// part of the native protocol; account selectors cannot enter the SQL builder.
func PatchInformation(s *persistence.Store, account string, fields map[string]string) error {
	columns, values, err := informationUpdate(fields)
	if err != nil {
		return err
	}
	return s.Write(func(tx *sql.Tx) error { return patchInformation(tx, account, columns, values) })
}

func informationUpdate(fields map[string]string) ([]string, []any, error) {
	columns := []string{}
	values := []any{}
	for _, name := range []string{"email", "email_flag", "country", "firstname", "lastname", "sex", "birthday"} {
		value, exists := fields[name]
		if !exists {
			continue
		}
		if !informationText(value) {
			return nil, nil, errors.New("invalid profile text")
		}
		if name == "email_flag" || name == "sex" {
			if value != "0" && value != "1" {
				return nil, nil, errors.New("invalid profile flag")
			}
		}
		if name == "email" && value != "" {
			address, err := mail.ParseAddress(value)
			if err != nil || address.Address != value || address.Name != "" {
				return nil, nil, errors.New("invalid profile email")
			}
		}
		columns = append(columns, name+"=?")
		values = append(values, value)
	}
	if len(columns) == 0 {
		return nil, nil, errors.New("empty profile update")
	}
	return columns, values, nil
}

func patchInformation(tx *sql.Tx, account string, columns []string, values []any) error {
	if _, err := tx.Exec("INSERT INTO account_information(account) VALUES(?) ON CONFLICT(account) DO NOTHING", account); err != nil {
		return err
	}
	values = append(values, account)
	_, err := tx.Exec("UPDATE account_information SET "+strings.Join(columns, ",")+" WHERE account=?", values...)
	return err
}
