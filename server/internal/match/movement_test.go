package match

import (
	"encoding/binary"
	"fmt"
	"testing"
	"time"

	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/rooms"
)

func movementFixture(t *testing.T, id byte) *Bound {
	t.Helper()
	registry := &rooms.Registry{}
	r, err := registry.Create(rooms.Player{Account: "a", ID: "a", Name: "alice"}, rooms.Settings{Capacity: 2})
	if err != nil {
		t.Fatal(err)
	}
	if _, err = registry.Join(rooms.Player{Account: "b", ID: "b", Name: "bob"}, r.ID); err != nil {
		t.Fatal(err)
	}
	reservation, err := registry.Reserve("a")
	if err != nil {
		t.Fatal(err)
	}
	b, err := NewMovement(reservation, Options{Details: game.Details{Name: "Room", Beacon: [4]byte{id, 0, 2, 0}}, Loading: game.Loading{Map: id, Capacity: 2}}, func(token string) (string, error) { return token, nil }, time.Unix(100, 0))
	if err != nil {
		reservation.Release()
		t.Fatal(err)
	}
	t.Cleanup(b.Close)
	return b
}

func TestMovementMapsHaveDistinctRealFFAStarts(t *testing.T) {
	for id := byte(0); id < 6; id++ {
		t.Run(fmt.Sprint(id), func(t *testing.T) {
			b := movementFixture(t, id)
			spawns := b.session.movement.spawns
			if len(spawns) != 2 || spawns[0].ID == spawns[1].ID || spawns[0].Team != 0 || !spawns[0].Enabled {
				t.Fatal("invalid starts", spawns)
			}
		})
	}
}

func TestMovementStartsOnlyAfterBarrierAndOwnsPublication(t *testing.T) {
	b := movementFixture(t, 0)
	s := b.session
	now := time.Unix(100, 0)
	s.epoch = now
	var out []Delivery
	if err := s.advanceMovement(now, &out); err != nil || len(out) != 0 {
		t.Fatal(out, err)
	}
	s.phase = Synchronized
	if err := s.advanceMovement(now.Add(4*time.Second), &out); err != nil || len(out) != 2 {
		t.Fatal(out, err)
	}
	for i, d := range out {
		p, err := game.Decode(d.Data)
		if err != nil || !p.Header.Reliable || p.Messages[0][0] != game.StartGame || binary.BigEndian.Uint32(p.Messages[0][13:]) != s.movement.spawns[i].ID {
			t.Fatal(p, err)
		}
	}
	if s.phase != Playing {
		t.Fatal(s.phase)
	}
	pose := s.movement.poses[0]
	pose.Sequence = 10
	pose.Time = 4001
	pose.Position[0] += 1.25
	if err := s.propose(s.peers[0].member.Endpoint, []pc.Snapshot{pose}, now.Add(4001*time.Millisecond)); err != nil {
		t.Fatal(err)
	}
	for _, bad := range []pc.Snapshot{
		pose,
		{Slot: 1, Generation: 1, Sequence: 11, Time: 4002},
		{Slot: 0, Generation: 2, Sequence: 11, Time: 4002},
		{Slot: 0, Generation: 1, Sequence: 11, Time: 90000},
	} {
		if err := s.propose(s.peers[0].member.Endpoint, []pc.Snapshot{bad}, now.Add(4002*time.Millisecond)); err == nil {
			t.Fatal("invalid proposal accepted", bad)
		}
	}
	lives, body, err := s.publication(now.Add(4002 * time.Millisecond))
	if err != nil {
		t.Fatal(err)
	}
	declared, err := pc.DecodeLives(lives)
	if err != nil || len(declared) != 2 {
		t.Fatal(declared, err)
	}
	poses, err := pc.DecodeSnapshots(body)
	if err != nil || poses[0].Sequence != 1 || poses[0].Time != 4002 || poses[0].Position != pose.Position {
		t.Fatal(poses, err)
	}
	if l, p, e := s.publication(now.Add(4003 * time.Millisecond)); l != nil || p != nil || e != nil {
		t.Fatal("publication burst")
	}
	if _, _, err = s.publication(now.Add(8 * time.Second)); err != nil {
		t.Fatal(err)
	}
	if s.movement.poses[0].Sequence != 2 {
		t.Fatal("stale catch-up snapshots")
	}
	stats := s.Snapshot()
	if stats.PositionInputs != ([12]uint64{1}) || stats.PositionChanges != ([12]uint64{1}) || stats.SnapshotBatches != 2 {
		t.Fatal("movement diagnostics count rejected inputs or suppressed publications", stats)
	}
}
