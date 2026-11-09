// Package sessions owns admitted account lifetimes and lobby presence.
// Callers resolve public bearer credentials before entering this registry.
package sessions

import (
	"errors"
	"net"
	"sync"
	"time"
)

// Lease identifies one admitted lobby connection. Pointer identity prevents an
// old connection's cleanup from releasing a newer connection for the account.
type Lease struct {
	mu         sync.RWMutex
	account    string
	expires    time.Time
	connection net.Conn
}

func (l *Lease) Expires() time.Time { l.mu.RLock(); defer l.mu.RUnlock(); return l.expires }

type login struct {
	expires time.Time
	lease   *Lease
	state   int
}

// Legacy's zero value is ready for use. Close is terminal for this instance.
type Legacy struct {
	mu     sync.Mutex
	logins map[string]*login
	closed bool
}

// Login retains an existing unexpired login, including its deadline and lease.
// Repeated password login must not evict an active player or extend its lifetime.
func (s *Legacy) Login(account string, now time.Time, ttl time.Duration) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.expire(now)
	if s.closed || account == "" || ttl <= 0 {
		return errors.New("legacy session unavailable")
	}
	if s.logins == nil {
		s.logins = make(map[string]*login)
	}
	if s.logins[account] == nil {
		s.logins[account] = &login{expires: now.Add(ttl), state: 3}
	}
	return nil
}

func (s *Legacy) SetState(account string, state int, now time.Time) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.expire(now)
	entry := s.logins[account]
	if s.closed || entry == nil || state < 0 || state > 3 {
		return errors.New("invalid presence")
	}
	entry.state = state
	return nil
}

func (s *Legacy) State(account string, now time.Time) int {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.expire(now)
	if entry := s.logins[account]; !s.closed && entry != nil {
		return entry.state
	}
	return 0
}

// OnlineState is visible to other players. A retained HTTP authorization is
// not a live game connection: closing the admitted lobby must show offline
// immediately, even though credentials remain valid for a later login.
func (s *Legacy) OnlineState(account string, now time.Time) int {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.expire(now)
	if entry := s.logins[account]; !s.closed && entry != nil && entry.lease != nil {
		return entry.state
	}
	return 0
}

func (s *Legacy) Valid(account string, now time.Time) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.expire(now)
	return !s.closed && s.logins[account] != nil
}

func (s *Legacy) Renew(account string, now time.Time, ttl time.Duration) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.expire(now)
	entry := s.logins[account]
	if s.closed || entry == nil || ttl <= 0 {
		return errors.New("login unavailable")
	}
	entry.expires = now.Add(ttl)
	if entry.lease != nil {
		entry.lease.mu.Lock()
		entry.lease.expires = entry.expires
		entry.lease.mu.Unlock()
	}
	return nil
}

func (s *Legacy) Logout(account string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if entry := s.logins[account]; entry != nil && entry.lease != nil {
		entry.lease.connection.Close()
	}
	delete(s.logins, account)
}

func (s *Legacy) Acquire(account string, connection net.Conn, now time.Time) (*Lease, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.expire(now)
	entry := s.logins[account]
	if s.closed || entry == nil || entry.lease != nil || connection == nil {
		return nil, errors.New("legacy lobby session unavailable")
	}
	lease := &Lease{account: account, expires: entry.expires, connection: connection}
	entry.lease = lease
	return lease, nil
}

func (s *Legacy) Active(lease *Lease, now time.Time) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.expire(now)
	if s.closed || lease == nil {
		return false
	}
	entry := s.logins[lease.account]
	return entry != nil && entry.lease == lease
}

func (s *Legacy) Release(lease *Lease) {
	if lease == nil {
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if entry := s.logins[lease.account]; entry != nil && entry.lease == lease {
		entry.lease = nil
	}
}

func (s *Legacy) Count(now time.Time) int {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.expire(now)
	count := 0
	for _, entry := range s.logins {
		if entry.lease != nil {
			count++
		}
	}
	return count
}

func (s *Legacy) Close() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.closed = true
	for _, entry := range s.logins {
		if entry.lease != nil {
			entry.lease.connection.Close()
		}
	}
	clear(s.logins)
}

// Call with mu held. Closing a socket wakes its handler; Release takes mu only
// after Close returns. Expired authorizations never contribute to presence.
func (s *Legacy) expire(now time.Time) {
	for account, entry := range s.logins {
		if !entry.expires.After(now) {
			if entry.lease != nil {
				entry.lease.connection.Close()
			}
			delete(s.logins, account)
		}
	}
}
