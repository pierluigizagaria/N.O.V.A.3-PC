package match

import (
	"bytes"
	"errors"
	"fmt"
	"net/netip"
	"testing"
	"time"

	"nova3.test/backend/internal/protocol/game"
)

func TestZonesBoundToActorMapAndRetainedAcrossSceneActivation(t *testing.T) {
	s := movementFixture(t, 0).session
	now := time.Unix(100, 0)
	s.phase, s.deadline = Playing, now.Add(time.Minute)
	for _, p := range s.peers {
		p.joined, p.loaded = true, true
	}
	if len(s.movement.zones) == 0 {
		t.Fatal("compiled map has no zone identities")
	}
	var zone int32
	for id := range s.movement.zones {
		zone = id
		break
	}
	p := s.peers[0]
	v := game.ZoneChange{Zone: zone, Sequence: 252}
	m, _ := v.Message()
	// The player's local initialization can precede SceneReady. Retain it
	// without invoking a remote object that has not been announced yet.
	if out, err := s.Handle(p.member.Endpoint, wire(t, game.Header{}, m), now); err != nil || len(out) != 0 {
		t.Fatal(out, err)
	}
	if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true}, []byte{game.SceneReady}), now); err != nil {
		t.Fatal(err)
	}
	out, err := s.Handle(s.peers[1].member.Endpoint, wire(t, game.Header{Reliable: true}, []byte{game.SceneReady}), now)
	if err != nil {
		t.Fatal(err)
	}
	found := false
	for _, d := range out {
		if d.To != s.peers[1].member.Endpoint {
			continue
		}
		packet, _ := game.Decode(d.Data)
		for _, record := range packet.Messages {
			if bytes.Equal(record, m) {
				found = true
			}
		}
	}
	if !found {
		t.Fatal("newcomer did not receive current zone after activation")
	}
	for _, sequence := range []byte{253, 0, 1} {
		v.Sequence = sequence
		m, _ = v.Message()
		out, err = s.Handle(p.member.Endpoint, wire(t, game.Header{}, m), now)
		if err != nil || len(out) != 1 || out[0].Data[0] != 1 || !bytes.Equal(out[0].Data[13:], m) {
			t.Fatal(out, err)
		}
	}
	for _, sequence := range []byte{1, 0, 253} {
		v.Sequence = sequence
		m, _ = v.Message()
		out, err = s.Handle(p.member.Endpoint, wire(t, game.Header{}, m), now)
		if err != nil || len(out) != 0 {
			t.Fatal("stale zone relayed", out, err)
		}
	}
	for _, bad := range []game.ZoneChange{{Actor: 1, Object: 0, Zone: zone}, {Actor: 0, Object: 1, Zone: zone}, {Actor: 0, Object: 12, Zone: zone}, {Zone: 0x7fffffff}} {
		m, _ = bad.Message()
		if _, err = s.Handle(p.member.Endpoint, wire(t, game.Header{}, m), now); !errors.Is(err, ErrPeer) {
			t.Fatal(bad, err)
		}
	}
}

func TestFullRoomAdmissionCarriesAllPresentationRecords(t *testing.T) {
	s := movementFixture(t, 0).session
	now := time.Unix(100, 0)
	s.phase, s.deadline = Playing, now.Add(time.Minute)
	for i := len(s.peers); i < 12; i++ {
		s.peers = append(s.peers, &peer{player: game.Player{Name: fmt.Sprintf("p%d", i), Slot: byte(i)}, member: Member{Endpoint: netip.AddrPortFrom(netip.MustParseAddr("127.0.0.1"), uint16(i+1))}})
		s.movement.spawns = append(s.movement.spawns, s.movement.spawns[0])
	}
	for _, p := range s.peers {
		p.sceneReady, p.aiming = true, true
		p.zone = &game.ZoneChange{Actor: p.player.Slot, Object: p.player.Slot, Zone: 1, Sequence: 1}
	}
	last := s.peers[11]
	last.sceneReady = false
	var out []Delivery
	if err := s.activate(last, now, &out); err != nil {
		t.Fatal(err)
	}
	packet, err := game.Decode(out[len(out)-1].Data)
	if err != nil || len(packet.Messages) != 23 {
		t.Fatal("full-room admission lost records", len(packet.Messages), err)
	}
	if packet.Messages[0][0] != game.PlayersSnapshot {
		t.Fatal("snapshot must precede zone/aim state")
	}
	for _, m := range packet.Messages {
		if m[0] == game.NewPlayer {
			t.Fatal("duplicate roster addition")
		}
	}
}
