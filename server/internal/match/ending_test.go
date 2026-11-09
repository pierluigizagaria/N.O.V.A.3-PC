package match

import (
	"bytes"
	"errors"
	"net/netip"
	"testing"
	"time"

	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/protocol/secure"
)

func TestTimedMatchEndsReliablyAndStopsPublication(t *testing.T) {
	s := movementFixture(t, 0).session
	now := time.Unix(100, 0)
	s.phase, s.deadline, s.epoch = Synchronized, now.Add(time.Minute), now
	s.loading.Parameter1 = 1
	for _, p := range s.peers {
		p.joined, p.loaded, p.sceneReady = true, true, true
	}
	var start []Delivery
	if err := s.advanceMovement(now, &start); err != nil {
		t.Fatal(err)
	}
	if err := s.advanceRound(now, &start); err != nil {
		t.Fatal(err)
	}
	if err := s.advanceRound(now.Add(10*time.Second), &start); err != nil {
		t.Fatal(err)
	}
	if s.endsAt != now.Add(70*time.Second) {
		t.Fatal("time limit/countdown", s.endsAt)
	}
	for _, p := range s.peers {
		p.channel.pending = nil
	}
	if _, err := s.Tick(now.Add(69 * time.Second)); err != nil || s.phase != Playing {
		t.Fatal(s.phase, err)
	}
	out, err := s.Tick(now.Add(70 * time.Second))
	if err != nil || s.phase != Finishing || len(out) != 2 {
		t.Fatal(s.phase, out, err)
	}
	for _, d := range out {
		packet, err := game.Decode(d.Data)
		if err != nil || !packet.Header.Reliable || len(packet.Messages) != 1 || !bytes.Equal(packet.Messages[0], []byte{game.EndGame}) {
			t.Fatal(packet, err)
		}
	}
	if lives, poses, err := s.publication(now.Add(71 * time.Second)); err != nil || lives != nil || poses != nil {
		t.Fatal("published after end", err)
	}
	retries, err := s.Tick(now.Add(71 * time.Second))
	if err != nil || len(retries) != 2 || !bytes.Equal(retries[0].Data, out[0].Data) || !bytes.Equal(retries[1].Data, out[1].Data) {
		t.Fatal("lost ending not retried", retries, err)
	}
	for _, d := range out {
		packet, _ := game.Decode(d.Data)
		// A late gameplay command may carry the end ACK; ignore its action.
		body, _ := (game.Aim{Actor: s.byEndpoint[d.To].player.Slot, Enabled: true}).Message()
		if _, err = s.Handle(d.To, wire(t, game.Header{Ack: packet.Header.Sequence, AckMask: 1}, body), now.Add(71*time.Second)); err != nil {
			t.Fatal(err)
		}
		if s.byEndpoint[d.To].aiming {
			t.Fatal("ending accepted gameplay mutation")
		}
	}
	if _, err = s.Tick(now.Add(71 * time.Second)); err != nil || s.phase != Finishing {
		t.Fatal("revoked before client transition grace", s.phase, err)
	}
	if _, err = s.Tick(now.Add(72 * time.Second)); err != nil || s.phase != Finished {
		t.Fatal(s.phase, err)
	}
}

func TestEndGraceIsBoundedWithoutClientAck(t *testing.T) {
	s := movementFixture(t, 0).session
	now := time.Unix(100, 0)
	s.phase, s.endsAt = Playing, now
	for _, p := range s.peers {
		p.joined = true
	}
	if _, err := s.Tick(now); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Tick(now.Add(peerTimeout)); err != nil || s.phase != Finished {
		t.Fatal(s.phase, err)
	}
}

func TestUnlimitedRoomHasNoArtificialFifteenMinuteDeadline(t *testing.T) {
	s := movementFixture(t, 0).session
	now := time.Unix(100, 0)
	s.phase, s.deadline = Synchronized, now.Add(time.Minute)
	var out []Delivery
	if err := s.advanceMovement(now, &out); err != nil {
		t.Fatal(err)
	}
	for _, p := range s.peers {
		p.channel.pending = nil
	}
	if _, err := s.Tick(now.Add(16 * time.Minute)); err != nil || s.phase != Playing || !s.endsAt.IsZero() {
		t.Fatal(s.phase, err)
	}
	if _, err := s.Handle(s.peers[0].member.Endpoint, wire(t, game.Header{}, []byte{game.EndGame}), now.Add(16*time.Minute)); !errors.Is(err, ErrUnsupported) {
		t.Fatal("client ended game", err)
	}
}

func TestActiveTicketRenewalAndEndRelease(t *testing.T) {
	b := movementFixture(t, 0)
	now := time.Unix(100, 0)
	s := b.session
	s.phase, s.epoch = Playing, now
	for _, p := range s.peers {
		p.joined, p.loaded = true, true
	}
	channels := make([]*secure.Channel, 2)
	for i, account := range []string{"a", "b"} {
		ticket, err := b.Issue(account, pc.SnapshotV1, now)
		if err != nil {
			t.Fatal(err)
		}
		channels[i], err = secure.New(ticket, false)
		if err != nil {
			t.Fatal(err)
		}
	}
	send := func(i int, at time.Time, header game.Header) {
		t.Helper()
		plain, _ := pc.Encode(pc.Native, wire(t, header, []byte{game.Acknowledge}))
		data, _ := channels[i].Seal(plain)
		if _, err := b.Handle(netip.AddrPortFrom(netip.MustParseAddr("127.0.0.1"), uint16(4000+i)), data, at); err != nil {
			t.Fatal(err)
		}
	}
	for _, at := range []time.Time{now.Add(14 * time.Minute), now.Add(16 * time.Minute)} {
		for i := range channels {
			send(i, at, game.Header{})
		}
	}
	end := now.Add(16 * time.Minute)
	s.endsAt = end
	out, err := b.Tick(end)
	if err != nil || len(out) != 2 || b.Snapshot().Phase != Finishing {
		t.Fatal(out, err)
	}
	for i, d := range out {
		plain, err := channels[i].Open(d.Data)
		if err != nil {
			t.Fatal(err)
		}
		kind, body, _ := pc.Decode(plain)
		packet, _ := game.Decode(body)
		if kind != pc.Native || packet.Messages[0][0] != game.EndGame {
			t.Fatal(kind, packet)
		}
		send(i, end.Add(time.Second), game.Header{Ack: packet.Header.Sequence, AckMask: 1})
	}
	if _, err = b.Tick(end.Add(2 * time.Second)); err != nil || b.Snapshot().Phase != Finished {
		t.Fatal(b.Snapshot(), err)
	}
	select {
	case <-b.Done():
		t.Fatal("room released before completion owner")
	default:
	}
	b.Close()
	select {
	case <-b.Done():
	default:
		t.Fatal("finished room remained reserved")
	}
}
