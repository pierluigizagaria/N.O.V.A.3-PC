package sessions

import (
	"net"
	"sync"
	"testing"
	"time"
)

func pipe(t *testing.T) net.Conn {
	t.Helper()
	a, b := net.Pipe()
	t.Cleanup(func() { a.Close(); b.Close() })
	return a
}

func TestLeaseIsolationExpiryAndStaleRelease(t *testing.T) {
	var sessions Legacy
	now := time.Now()
	for _, id := range []string{"alice", "bob"} {
		if err := sessions.Login(id, now, time.Hour); err != nil {
			t.Fatal(err)
		}
	}
	a, err := sessions.Acquire("alice", pipe(t), now)
	if err != nil {
		t.Fatal(err)
	}
	b, err := sessions.Acquire("bob", pipe(t), now)
	if err != nil {
		t.Fatal(err)
	}
	if _, err = sessions.Acquire("alice", pipe(t), now); err == nil {
		t.Fatal("duplicate connection admitted")
	}
	if sessions.Count(now) != 2 || !sessions.Active(a, now) || !sessions.Active(b, now) {
		t.Fatal("duplicate damaged presence")
	}
	if err = sessions.Login("alice", now.Add(time.Minute), time.Hour); err != nil {
		t.Fatal(err)
	}
	if !sessions.Active(a, now) || a.Expires() != now.Add(time.Hour) {
		t.Fatal("repeated login changed active lease")
	}
	sessions.Release(a)
	replacement, err := sessions.Acquire("alice", pipe(t), now)
	if err != nil {
		t.Fatal(err)
	}
	sessions.Release(a)
	if !sessions.Active(replacement, now) {
		t.Fatal("stale cleanup released replacement")
	}
	if sessions.Count(now.Add(time.Hour)) != 0 || sessions.Valid("alice", now.Add(time.Hour)) {
		t.Fatal("expired presence retained")
	}
	if _, err = sessions.Acquire("alice", pipe(t), now.Add(time.Hour)); err == nil {
		t.Fatal("expired login admitted")
	}
	if err = sessions.Login("alice", now.Add(time.Hour), time.Hour); err != nil {
		t.Fatal(err)
	}
	newLease, err := sessions.Acquire("alice", pipe(t), now.Add(time.Hour))
	if err != nil {
		t.Fatal(err)
	}
	sessions.Release(replacement)
	if !sessions.Active(newLease, now.Add(time.Hour)) {
		t.Fatal("expired cleanup released new login")
	}
	sessions.Close()
	if sessions.Count(now) != 0 || sessions.Valid("alice", now) {
		t.Fatal("closed registry retained state")
	}
	if err = sessions.Login("alice", now, time.Hour); err == nil {
		t.Fatal("closed registry reopened")
	}
}

func TestConcurrentAcquireHasOneWinner(t *testing.T) {
	var sessions Legacy
	now := time.Now()
	if err := sessions.Login("alice", now, time.Hour); err != nil {
		t.Fatal(err)
	}
	defer sessions.Close()
	connections := make([]net.Conn, 16)
	for i := range connections {
		connections[i] = pipe(t)
	}
	var workers sync.WaitGroup
	winners := make(chan *Lease, len(connections))
	for _, connection := range connections {
		workers.Add(1)
		go func() {
			defer workers.Done()
			if lease, err := sessions.Acquire("alice", connection, now); err == nil {
				winners <- lease
			}
		}()
	}
	workers.Wait()
	close(winners)
	if len(winners) != 1 || sessions.Count(now) != 1 {
		t.Fatal("concurrent duplicate admission")
	}
}

func TestKeepAliveExtendsLeaseAndLogoutInvalidatesIt(t *testing.T) {
	var registry Legacy
	now := time.Now()
	if err := registry.Login("alice", now, time.Hour); err != nil {
		t.Fatal(err)
	}
	lease, err := registry.Acquire("alice", pipe(t), now)
	if err != nil {
		t.Fatal(err)
	}
	later := now.Add(50 * time.Minute)
	if err = registry.Renew("alice", later, time.Hour); err != nil {
		t.Fatal(err)
	}
	if lease.Expires() != later.Add(time.Hour) || !registry.Active(lease, now.Add(70*time.Minute)) {
		t.Fatal("keepalive did not extend active lease")
	}
	registry.Logout("alice")
	if registry.Active(lease, later) || registry.Valid("alice", later) {
		t.Fatal("logout kept lease")
	}
	if err = registry.Renew("alice", later, time.Hour); err == nil {
		t.Fatal("renewal resurrected logged-out account")
	}
}
