package rooms

import (
	"sync"
	"testing"
)

func TestAtomicMembershipSnapshotsAndOwnerDeparture(t *testing.T) {
	var s Registry
	a := Player{Account: "alice", ID: "p1", Name: "Alice"}
	b := Player{Account: "bob", ID: "p2", Name: "Bob"}
	r, err := s.Create(a, Settings{Capacity: 2, Data: "original-settings"})
	if err != nil {
		t.Fatal(err)
	}
	r.Players[0].Name = "mutated"
	if s.List()[0].Owner.Name != "Alice" || s.List()[0].Players[0].Name != "Alice" {
		t.Fatal("snapshot aliases storage")
	}
	if _, err = s.Create(a, Settings{Capacity: 2}); err != ErrMember {
		t.Fatal(err)
	}
	var wg sync.WaitGroup
	results := make(chan error, 16)
	for i := 0; i < 16; i++ {
		wg.Add(1)
		go func() { defer wg.Done(); _, e := s.Join(b, r.ID); results <- e }()
	}
	wg.Wait()
	close(results)
	winners := 0
	for e := range results {
		if e == nil {
			winners++
		} else if e != ErrMember {
			t.Fatal(e)
		}
	}
	if winners != 1 {
		t.Fatal(winners)
	}
	if _, err = s.Join(Player{Account: "carol", ID: "p3"}, r.ID); err != ErrFull {
		t.Fatal(err)
	}
	if old, ok := s.Leave("alice"); !ok || len(old.Players) != 2 || len(s.List()) != 0 {
		t.Fatal("owner departure did not destroy room")
	}
	if _, err = s.Current("bob"); err != ErrMissing {
		t.Fatal("stale membership")
	}
	if _, err = s.Create(b, Settings{Capacity: 2}); err != nil {
		t.Fatal("cannot create after owner departure", err)
	}
}
