package rooms

import (
	"sync"
	"testing"
)

func TestMatchReservationFreezesAndInvalidatesMembership(t *testing.T) {
	var registry Registry
	r, _ := registry.Create(Player{Account: "a", ID: "p1", Name: "Alice"}, Settings{Capacity: 3})
	registry.Join(Player{Account: "b", ID: "p2", Name: "Bob"}, r.ID)
	if _, err := registry.Reserve("b"); err != ErrOwner {
		t.Fatal(err)
	}
	lease, err := registry.Reserve("a")
	if err != nil {
		t.Fatal(err)
	}
	if _, err = registry.Reserve("a"); err != ErrBusy {
		t.Fatal(err)
	}
	if _, err = registry.Join(Player{Account: "c", ID: "p3"}, r.ID); err != ErrBusy {
		t.Fatal(err)
	}
	status := byte(1)
	if _, _, err = registry.UpdatePlayer("b", nil, &status); err != ErrBusy {
		t.Fatal(err)
	}
	if _, err = registry.UpdateSettings("a", "new", nil); err != ErrBusy {
		t.Fatal(err)
	}
	if len(registry.List()) != 0 {
		t.Fatal("reserved room still advertised")
	}
	copy := lease.Snapshot()
	copy.Players[0].Name = "Eve"
	if p, ok := lease.Member("a"); !ok || p.Name != "Alice" {
		t.Fatal("mutable membership snapshot")
	}
	registry.Leave("b")
	select {
	case <-lease.Done():
	default:
		t.Fatal("departure did not revoke match")
	}
	if _, ok := lease.Member("a"); ok {
		t.Fatal("stale authorization")
	}
	newLease, err := registry.Reserve("a")
	if err != nil {
		t.Fatal(err)
	}
	lease.Release()
	if _, ok := newLease.Member("a"); !ok {
		t.Fatal("stale cleanup revoked new match")
	}
	registry.Leave("a")
	select {
	case <-newLease.Done():
	default:
		t.Fatal("owner departure did not revoke match")
	}
}

func TestConcurrentReserveHasOneOwner(t *testing.T) {
	var r Registry
	r.Create(Player{Account: "a", ID: "p1"}, Settings{Capacity: 2})
	var wg sync.WaitGroup
	results := make(chan *Reservation, 16)
	for i := 0; i < 16; i++ {
		wg.Add(1)
		go func() { defer wg.Done(); lease, _ := r.Reserve("a"); results <- lease }()
	}
	wg.Wait()
	close(results)
	winners := 0
	for lease := range results {
		if lease != nil {
			winners++
			lease.Release()
		}
	}
	if winners != 1 {
		t.Fatal(winners)
	}
}

func TestRunningReservationSurvivesOwnerAndMemberDeparture(t *testing.T) {
	var r Registry
	room, _ := r.Create(Player{Account: "a", ID: "p1"}, Settings{Capacity: 3})
	r.Join(Player{Account: "b", ID: "p2"}, room.ID)
	r.Join(Player{Account: "c", ID: "p3"}, room.ID)
	lease, _ := r.Reserve("a")
	if !lease.Start() {
		t.Fatal("cannot commit allocation")
	}
	r.Leave("a")
	current, err := r.Current("b")
	if err != nil || current.Owner.Account != "b" || len(current.Players) != 2 || !current.Reserved() {
		t.Fatal(current, err)
	}
	if _, ok := lease.Member("a"); ok {
		t.Fatal("departed owner retained authority")
	}
	if _, ok := lease.Member("b"); !ok {
		t.Fatal("survivor lost authority")
	}
	if len(lease.Snapshot().Players) != 3 {
		t.Fatal("frozen admission slots changed")
	}
	if len(r.List()) != 1 {
		t.Fatal("live room missing")
	}
	if _, err = r.Join(Player{Account: "a", ID: "p1"}, room.ID); err != nil {
		t.Fatal("live membership denied", err)
	}
	r.Leave("a")
	r.Leave("c")
	select {
	case <-lease.Done():
		t.Fatal("running server depends on a client")
	default:
	}
	r.Leave("b")
	select {
	case <-lease.Done():
	default:
		t.Fatal("empty match retained")
	}
	if lease.Start() {
		t.Fatal("closed reservation restarted")
	}
	if _, err = r.Get(room.ID); err != ErrMissing {
		t.Fatal("empty room retained", err)
	}
}
