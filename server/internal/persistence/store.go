// Package persistence owns SQLite setup and atomic state transitions.
package persistence

import (
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"

	_ "modernc.org/sqlite"
	"nova3.test/backend/internal/catalogue"
)

type Store struct{ DB *sql.DB }

func Open(path string) (*Store, error) {
	if path == "" || path == ":memory:" {
		return nil, errors.New("explicit file database path required")
	}
	absolute, err := filepath.Abs(path)
	if err != nil {
		return nil, err
	}
	if err = os.MkdirAll(filepath.Dir(absolute), 0700); err != nil {
		return nil, err
	}
	// A filename, not a caller-controlled SQLite URI.
	db, err := sql.Open("sqlite", absolute)
	if err != nil {
		return nil, err
	}
	db.SetMaxOpenConns(1)
	fail := func(e error) (*Store, error) { db.Close(); return nil, e }
	for _, pragma := range []string{"PRAGMA foreign_keys=ON", "PRAGMA busy_timeout=5000", "PRAGMA journal_mode=WAL"} {
		if _, err = db.Exec(pragma); err != nil {
			return fail(err)
		}
	}
	var version int
	if err = db.QueryRow("PRAGMA user_version").Scan(&version); err != nil {
		return fail(err)
	}
	if version > 1 {
		return fail(errors.New("database schema is newer than this application"))
	}
	if version == 0 {
		tx, e := db.Begin()
		if e != nil {
			return fail(e)
		}
		_, e = tx.Exec(schema)
		if e != nil {
			tx.Rollback()
			return fail(e)
		}
		if e = tx.Commit(); e != nil {
			return fail(e)
		}
	}
	return &Store{DB: db}, nil
}
func (s *Store) Close() error { return s.DB.Close() }
func (s *Store) Write(f func(*sql.Tx) error) error {
	tx, err := s.DB.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	if err = f(tx); err != nil {
		return err
	}
	return tx.Commit()
}
func (s *Store) Admit(c catalogue.Catalogue) error {
	if err := c.Validate(); err != nil {
		return err
	}
	data, err := json.Marshal(c)
	if err != nil {
		return err
	}
	return s.Write(func(tx *sql.Tx) error {
		var old string
		e := tx.QueryRow("SELECT body FROM catalogues WHERE version=?", c.Version).Scan(&old)
		if e == nil {
			if old != string(data) {
				return errors.New("catalogue version is immutable")
			}
			return nil
		}
		if !errors.Is(e, sql.ErrNoRows) {
			return e
		}
		_, e = tx.Exec("INSERT INTO catalogues(version,body) VALUES(?,?)", c.Version, string(data))
		return e
	})
}

type Querier interface{ QueryRow(string, ...any) *sql.Row }

func Catalogue(q Querier, version string) (catalogue.Catalogue, error) {
	var body string
	if err := q.QueryRow("SELECT body FROM catalogues WHERE version=?", version).Scan(&body); err != nil {
		return catalogue.Catalogue{}, fmt.Errorf("catalogue unavailable: %w", err)
	}
	return catalogue.Decode([]byte(body))
}

const schema = `
CREATE TABLE catalogues(version TEXT PRIMARY KEY,body TEXT NOT NULL);
CREATE TABLE accounts(id TEXT PRIMARY KEY,name TEXT NOT NULL UNIQUE);
CREATE TABLE profiles(account TEXT PRIMARY KEY REFERENCES accounts(id),version TEXT NOT NULL REFERENCES catalogues(version));
CREATE TABLE entitlements(account TEXT NOT NULL REFERENCES profiles(account),item TEXT NOT NULL,quantity INTEGER NOT NULL CHECK(quantity>=0),PRIMARY KEY(account,item));
CREATE TABLE loadouts(account TEXT PRIMARY KEY REFERENCES profiles(account),body TEXT NOT NULL);
CREATE TABLE sessions(hash TEXT PRIMARY KEY,account TEXT NOT NULL REFERENCES accounts(id),expires INTEGER NOT NULL);
PRAGMA user_version=1;
`
