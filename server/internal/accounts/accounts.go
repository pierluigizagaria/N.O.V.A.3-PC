// Package accounts provides community identities and revocable opaque sessions.
package accounts

import (
	"crypto/rand"
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"errors"
	"fmt"
	"time"

	"nova3.test/backend/internal/catalogue"
	"nova3.test/backend/internal/persistence"
)

type Account struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}

func randomToken() (string, error) {
	var b [32]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", err
	}
	return hex.EncodeToString(b[:]), nil
}
func Create(s *persistence.Store, name string) (Account, error) {
	if !catalogue.ValidID(name) {
		return Account{}, errors.New("invalid test account name")
	}
	id, err := randomToken()
	if err != nil {
		return Account{}, err
	}
	a := Account{id, name}
	_, err = s.DB.Exec("INSERT INTO accounts(id,name) VALUES(?,?)", a.ID, a.Name)
	if err != nil {
		return Account{}, fmt.Errorf("create account: %w", err)
	}
	return a, nil
}
func hash(token string) string { v := sha256.Sum256([]byte(token)); return hex.EncodeToString(v[:]) }
func Issue(s *persistence.Store, account string, ttl time.Duration) (string, error) {
	if ttl < time.Second || ttl > 24*time.Hour {
		return "", errors.New("session TTL must be 1 second..24 hours")
	}
	token, err := randomToken()
	if err != nil {
		return "", err
	}
	_, err = s.DB.Exec("INSERT INTO sessions(hash,account,expires) VALUES(?,?,?)", hash(token), account, time.Now().Add(ttl).Unix())
	if err != nil {
		return "", err
	}
	return token, nil
}
func Resolve(s *persistence.Store, token string) (string, error) {
	return ResolveWith(s.DB, token)
}

// ResolveWith lets a state change validate its session in the same transaction.
func ResolveWith(q persistence.Querier, token string) (string, error) {
	if len(token) != 64 {
		return "", errors.New("invalid session")
	}
	var id string
	err := q.QueryRow("SELECT account FROM sessions WHERE hash=? AND expires>?", hash(token), time.Now().Unix()).Scan(&id)
	if err != nil {
		return "", errors.New("invalid or expired session")
	}
	return id, nil
}
func Revoke(s *persistence.Store, token string) error {
	_, err := s.DB.Exec("DELETE FROM sessions WHERE hash=?", hash(token))
	return err
}

// Refresh extends only a still-valid credential; it cannot resurrect a revoked
// or expired login. The caller has already authenticated the native keepalive.
func Refresh(s *persistence.Store, token string, ttl time.Duration) error {
	return RefreshWith(s.DB, token, ttl)
}

func RefreshWith(q interface {
	Exec(string, ...any) (sql.Result, error)
}, token string, ttl time.Duration) error {
	if len(token) != 64 || ttl < time.Second || ttl > 24*time.Hour {
		return errors.New("invalid session renewal")
	}
	now := time.Now()
	result, err := q.Exec("UPDATE sessions SET expires=? WHERE hash=? AND expires>?", now.Add(ttl).Unix(), hash(token), now.Unix())
	if err != nil {
		return err
	}
	if n, _ := result.RowsAffected(); n != 1 {
		return errors.New("session expired or revoked")
	}
	return nil
}

func RevokeAccount(s *persistence.Store, account string) error {
	_, err := s.DB.Exec("DELETE FROM sessions WHERE account=?", account)
	return err
}
