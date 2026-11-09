package match

import (
	"bytes"
	"encoding/binary"
	"errors"
	"nova3.test/backend/internal/protocol/game"
	"testing"
	"time"
)

func TestEquippedWeaponAndAimAreReplicatedAndIncludedForNewScene(t *testing.T) {
	s := movementFixture(t, 0).session
	now := time.Unix(100, 0)
	s.phase, s.deadline = Playing, now.Add(time.Minute)
	s.movement.primaryWeapons = map[byte]uint16{1: 11, 2: 12}
	s.movement.secondaryWeapons = map[byte]uint16{1: 21}
	for _, p := range s.peers {
		p.joined, p.loaded = true, true
		p.weapon = 11
		p.player.Beacon[6], p.player.Beacon[9] = 1, 1
	}
	p := s.peers[0]
	p.sceneReady = true
	change, _ := (game.WeaponAction{Actor: 0, Value: 21}).Message(game.ChangeWeapon)
	for _, body := range [][]byte{change, {game.AimWeapon, 0x80}} {
		if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{}, body), now); err != nil {
			t.Fatal(err)
		}
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
		packet, err := game.Decode(d.Data)
		if err != nil {
			t.Fatal(err)
		}
		for _, m := range packet.Messages {
			if m[0] == game.PlayersSnapshot && binary.BigEndian.Uint16(m[25+57:]) != 21 {
				t.Fatal("snapshot lost selected secondary")
			}
			if bytes.Equal(m, []byte{game.AimWeapon, 0x80}) {
				found = true
			}
		}
	}
	if !found {
		t.Fatal("newly loaded peer missed current aim")
	}
	change, _ = (game.WeaponAction{Actor: 0, Value: 11}).Message(game.ChangeWeapon)
	out, err = s.Handle(p.member.Endpoint, wire(t, game.Header{}, change), now)
	if err != nil || len(out) != 1 || !bytes.Equal(out[0].Data[13:], change) || out[0].Data[0] != 1 {
		t.Fatal("weapon not reliably replicated", out, err)
	}
	for _, action := range []game.WeaponAction{{Actor: 0, Value: 12}, {Actor: 0, Value: -1}, {Actor: 0, Value: 65536}, {Actor: 1, Value: 11}} {
		body, _ := action.Message(game.ChangeWeapon)
		if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{}, body), now); !errors.Is(err, ErrPeer) {
			t.Fatal("unequipped or foreign weapon admitted", action, err)
		}
	}
}

func TestSceneReadyAnnouncesBothPlayersAndRetriesWithoutDuplicateActivation(t *testing.T) {
	s := movementFixture(t, 0).session
	now := time.Unix(100, 0)
	s.phase, s.deadline = Playing, now.Add(time.Minute)
	for _, p := range s.peers {
		p.joined, p.loaded = true, true
	}
	ready := wire(t, game.Header{Reliable: true}, []byte{game.SceneReady})
	out, err := s.Handle(s.peers[0].member.Endpoint, ready, now)
	if err != nil || len(out) != 2 || out[0].Data[13] != game.PlayersSnapshot || out[1].Data[13] != game.Acknowledge {
		t.Fatal(out, err)
	}
	out, err = s.Handle(s.peers[1].member.Endpoint, ready, now)
	if err != nil || len(out) != 3 || s.Snapshot().Active != 2 {
		t.Fatal(out, err)
	}
	for i, d := range out[:2] {
		p, err := game.Decode(d.Data)
		if err != nil || !p.Header.Reliable {
			t.Fatal(p, err)
		}
		if d.To != s.peers[i].member.Endpoint {
			t.Fatal("wrong scene recipient", i)
		}
		if i == 0 {
			presence, err := game.ReadPresence(p.Messages[0])
			if err != nil || presence.Player != s.peers[1].player || presence.Spawn != s.movement.spawns[1].ID {
				t.Fatal("wrong new peer identity", presence, err)
			}
		} else {
			if len(p.Messages) != 1 || p.Messages[0][0] != game.PlayersSnapshot {
				t.Fatal("newcomer would receive duplicate roster additions", p.Messages)
			}
			for slot := 0; slot < 2; slot++ {
				row := p.Messages[0][25+slot*63:]
				if row[0] != 1 || row[1] != 1 || row[3] != byte(slot) {
					t.Fatal("initial snapshot lost occupied slot", slot)
				}
			}
		}
	}
	out, err = s.Handle(s.peers[1].member.Endpoint, ready, now.Add(time.Millisecond))
	if err != nil || len(out) != 1 {
		t.Fatal("duplicate activation", out, err)
	}
	out, err = s.Tick(now.Add(time.Second))
	if err != nil || len(out) != 5 {
		t.Fatal("lost announcement not retried or round countdown missing", len(out), err)
	}
	countdowns := 0
	for _, d := range out {
		if d.Data[13] == game.RoundCountdown {
			countdowns++
			continue
		}
		if d.Data[13] != game.NewPlayer && d.Data[13] != game.PlayersSnapshot {
			t.Fatal(d)
		}
	}
	if countdowns != 2 {
		t.Fatal("round countdown was not sent to both peers")
	}
	// A foreign slot and a server-only lifecycle message never acquire authority.
	for _, body := range [][]byte{{game.JetpackStart, 1}, out[0].Data[13:]} {
		_, err = s.Handle(s.peers[0].member.Endpoint, wire(t, game.Header{}, body), now.Add(time.Second))
		if !errors.Is(err, ErrPeer) && !errors.Is(err, ErrUnsupported) {
			t.Fatal("unauthorized command", err)
		}
	}
}
