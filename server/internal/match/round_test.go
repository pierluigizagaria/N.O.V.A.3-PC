package match

import (
	"bytes"
	"encoding/binary"
	"nova3.test/backend/internal/protocol/game"
	"testing"
	"time"
)

func TestRoundWaitsForTwoScenesCountsDownAndPausesOnDeparture(t *testing.T) {
	s := movementFixture(t, 0).session
	now := time.Unix(100, 0)
	s.phase, s.epoch = Playing, now
	s.loading.Parameter1 = 1
	for _, p := range s.peers {
		p.joined, p.loaded = true, true
	}
	var out []Delivery
	s.peers[0].sceneReady = true
	if err := s.advanceRound(now, &out); err != nil || len(out) != 0 || !s.endsAt.IsZero() {
		t.Fatal(out, err)
	}
	s.peers[1].sceneReady = true
	if err := s.advanceRound(now, &out); err != nil || len(out) != 2 {
		t.Fatal(out, err)
	}
	for _, d := range out {
		p, err := game.Decode(d.Data)
		if err != nil || !p.Header.Reliable || p.Messages[0][0] != game.RoundCountdown || binary.BigEndian.Uint32(p.Messages[0][1:]) != 10000 {
			t.Fatal(p, err)
		}
	}
	clock := s.roundClock(now.Add(time.Second))
	if clock.Running || !clock.CountingDown || clock.Elapsed != 0 || clock.CountdownAt != 10000 {
		t.Fatal(clock)
	}
	out = nil
	if err := s.advanceRound(now.Add(9999*time.Millisecond), &out); err != nil || len(out) != 0 {
		t.Fatal(out, err)
	}
	if err := s.advanceRound(now.Add(10*time.Second), &out); err != nil || len(out) != 2 || s.endsAt != now.Add(70*time.Second) {
		t.Fatal(out, err, s.endsAt)
	}
	for _, d := range out {
		if !bytes.Equal(d.Data[13:], []byte{game.RoundStarted}) {
			t.Fatal(d)
		}
	}
	clock = s.roundClock(now.Add(15 * time.Second))
	if !clock.Running || clock.CountingDown || clock.Elapsed != 5000 {
		t.Fatal(clock)
	}
	s.peers[1].sceneReady = false
	out = nil
	if err := s.advanceRound(now.Add(15*time.Second), &out); err != nil || len(out) != 1 || !bytes.Equal(out[0].Data[13:], []byte{game.RoundWaiting}) || !s.endsAt.IsZero() {
		t.Fatal(out, err)
	}
	clock = s.roundClock(now.Add(time.Minute))
	if clock.Running || clock.CountingDown || clock.Elapsed != 5000 {
		t.Fatal("waiting consumed play time", clock)
	}
}

func TestPowerupRequestCompletesOriginalRoundClockHandshake(t *testing.T) {
	s := movementFixture(t, 0).session
	now := time.Unix(100, 0)
	s.phase, s.epoch = Playing, now
	p := s.peers[0]
	p.joined, p.loaded, p.sceneReady = true, true, true
	s.round.countdown = now.Add(10 * time.Second)
	out, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true}, []byte{game.RequestPowerups}), now)
	if err != nil || len(out) != 2 {
		t.Fatal(out, err)
	}
	packet, err := game.Decode(out[0].Data)
	if err != nil || !packet.Header.Reliable {
		t.Fatal(packet, err)
	}
	clock, err := game.ReadRoundClock(packet.Messages[0])
	if err != nil || !clock.CountingDown || clock.CountdownAt != 10000 {
		t.Fatal(clock, err)
	}
	// The reliable queue replays its response; a duplicate request only needs ACK.
	out, err = s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true}, []byte{game.RequestPowerups}), now)
	if err != nil || len(out) != 1 || out[0].Data[13] != game.Acknowledge {
		t.Fatal(out, err)
	}
}

func TestRoundSpawnRequiresServerGrantAndCannotBeRepeated(t *testing.T) {
	s := movementFixture(t, 0).session
	now := time.Unix(100, 0)
	s.phase, s.epoch = Playing, now
	for _, p := range s.peers {
		p.joined, p.loaded, p.sceneReady = true, true, true
	}
	p := s.peers[0]
	send := func(sequence uint32, slot byte) ([]Delivery, error) {
		return s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: sequence}, []byte{game.RequestRespawn, slot}), now)
	}
	if out, err := send(0, 0); err != nil || len(out) != 1 || s.movement.poses[0].Generation != 1 {
		t.Fatal("unsolicited respawn", out, err)
	}
	var out []Delivery
	if err := s.advanceRound(now, &out); err != nil {
		t.Fatal(err)
	}
	now = now.Add(nativeRoundCountdown)
	if err := s.advanceRound(now, &out); err != nil {
		t.Fatal(err)
	}
	if _, err := send(1, 1); err != ErrPeer {
		t.Fatal("spoofed actor", err)
	}
	s.movement.poses[0].Position[0] += 500
	s.movement.seen[0], s.movement.upstream[0] = true, 1000
	out, err := send(1, 0)
	if err != nil || len(out) != 3 {
		t.Fatal(out, err)
	}
	for _, d := range out[:2] {
		packet, err := game.Decode(d.Data)
		if err != nil || !packet.Header.Reliable {
			t.Fatal(packet, err)
		}
		r, err := game.ReadRespawn(packet.Messages[0])
		if err != nil || r.Slot != 0 || r.Spawn != s.movement.spawns[0].ID || r.Time != 10000 {
			t.Fatal(r, err)
		}
	}
	pose := s.movement.poses[0]
	if pose.Generation != 2 || pose.Position[0] != float32(s.movement.spawns[0].Transform.Position.X) || s.movement.seen[0] || s.movement.upstream[0] != 0 {
		t.Fatal(pose)
	}
	for _, sequence := range []uint32{1, 2} {
		if out, err := send(sequence, 0); err != nil || len(out) != 1 || s.movement.poses[0] != pose {
			t.Fatal("repeated spawn", out, err)
		}
	}
}

func TestRoundXPNotificationIsAcknowledgedWithoutTrustingClientScore(t *testing.T) {
	s := movementFixture(t, 0).session
	now := time.Unix(100, 0)
	s.phase, s.epoch = Playing, now
	p := s.peers[0]
	p.joined, p.loaded, p.sceneReady = true, true, true
	before := s.movement.poses[0]
	body := []byte{game.ClientXP, 1, 255, 255, 255, 255}
	if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true}, body), now); err != ErrPeer {
		t.Fatal("XP actor spoof", err)
	}
	body[1] = 0
	out, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true}, body), now)
	if err != nil || len(out) != 1 || out[0].Data[13] != game.Acknowledge || s.movement.poses[0] != before || p.roundSpawnPending {
		t.Fatal("XP notification changed authority", out, err)
	}
}
