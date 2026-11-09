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
	if version < 0 || version > len(migrations) {
		return fail(errors.New("database schema is newer than this application"))
	}
	for version < len(migrations) {
		tx, e := db.Begin()
		if e != nil {
			return fail(e)
		}
		_, e = tx.Exec(migrations[version])
		if e != nil {
			tx.Rollback()
			return fail(e)
		}
		if e = tx.Commit(); e != nil {
			return fail(e)
		}
		version++
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

// Each migration commits its schema marker with its data changes.
var migrations = []string{
	schema,
	`CREATE TABLE native_equipment(account TEXT PRIMARY KEY REFERENCES profiles(account),equipment TEXT NOT NULL,skins TEXT NOT NULL); PRAGMA user_version=2;`,
	`CREATE TABLE credentials(account TEXT PRIMARY KEY REFERENCES accounts(id),salt BLOB NOT NULL,derived BLOB NOT NULL);
CREATE TABLE native_data(account TEXT NOT NULL REFERENCES profiles(account),key TEXT NOT NULL,body TEXT NOT NULL,PRIMARY KEY(account,key));
PRAGMA user_version=3;`,
	`CREATE TABLE lobby_identities(number INTEGER PRIMARY KEY AUTOINCREMENT,account TEXT NOT NULL UNIQUE REFERENCES accounts(id));
PRAGMA user_version=4;`,
	`CREATE TABLE contacts(owner TEXT NOT NULL REFERENCES accounts(id),target TEXT NOT NULL REFERENCES accounts(id),created INTEGER NOT NULL,PRIMARY KEY(owner,target),CHECK(owner<>target));
CREATE INDEX contacts_target ON contacts(target);
PRAGMA user_version=5;`,
	`CREATE TABLE player_stats(account TEXT PRIMARY KEY REFERENCES profiles(account),matches INTEGER NOT NULL DEFAULT 0,kills INTEGER NOT NULL DEFAULT 0,deaths INTEGER NOT NULL DEFAULT 0,xp INTEGER NOT NULL,wins INTEGER NOT NULL DEFAULT 0,headshots INTEGER NOT NULL DEFAULT 0,flags INTEGER NOT NULL DEFAULT 0,seconds INTEGER NOT NULL DEFAULT 0,shots INTEGER NOT NULL DEFAULT 0,hits INTEGER NOT NULL DEFAULT 0,resets INTEGER NOT NULL);
CREATE TABLE scored_matches(id TEXT PRIMARY KEY,digest TEXT NOT NULL);
PRAGMA user_version=6;`,
	`CREATE TABLE messages(id INTEGER PRIMARY KEY AUTOINCREMENT,sender TEXT NOT NULL REFERENCES accounts(id),recipient TEXT NOT NULL REFERENCES accounts(id),type INTEGER NOT NULL,title TEXT NOT NULL,body TEXT NOT NULL,created INTEGER NOT NULL,is_read INTEGER NOT NULL DEFAULT 0,inbox_deleted INTEGER NOT NULL DEFAULT 0,sent_deleted INTEGER NOT NULL DEFAULT 0);
CREATE INDEX messages_inbox ON messages(recipient,inbox_deleted,id);
CREATE INDEX messages_sent ON messages(sender,sent_deleted,id);
PRAGMA user_version=7;`,
	`CREATE TABLE account_information(account TEXT PRIMARY KEY REFERENCES accounts(id),email TEXT NOT NULL DEFAULT '',email_flag INTEGER NOT NULL DEFAULT 0,country TEXT NOT NULL DEFAULT '',firstname TEXT NOT NULL DEFAULT '',lastname TEXT NOT NULL DEFAULT '',sex INTEGER NOT NULL DEFAULT 0,birthday TEXT NOT NULL DEFAULT '');
PRAGMA user_version=8;`,
	`CREATE TABLE public_user_data(account TEXT NOT NULL REFERENCES accounts(id),key TEXT NOT NULL,body TEXT NOT NULL,PRIMARY KEY(account,key));
PRAGMA user_version=9;`,
	`CREATE TABLE wallets(account TEXT PRIMARY KEY REFERENCES profiles(account),coins INTEGER NOT NULL DEFAULT 0 CHECK(coins>=0 AND coins<=2147483647));
PRAGMA user_version=10;`,
	`CREATE TABLE match_progress(match TEXT NOT NULL,account TEXT NOT NULL REFERENCES profiles(account),body TEXT NOT NULL,PRIMARY KEY(match,account));
PRAGMA user_version=11;`,
	`CREATE TABLE account_avatars(account TEXT PRIMARY KEY REFERENCES accounts(id),key TEXT NOT NULL UNIQUE,body BLOB NOT NULL);
PRAGMA user_version=12;`,
	`CREATE TABLE consumable_purchases(account TEXT NOT NULL REFERENCES profiles(account),nonce TEXT NOT NULL,kind INTEGER NOT NULL,accepted INTEGER NOT NULL,PRIMARY KEY(account,nonce));
PRAGMA user_version=13;`,
}
