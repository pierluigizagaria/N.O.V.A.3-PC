package match

import (
	"bytes"
	"errors"
	"nova3.test/backend/internal/protocol/game"
	"testing"
	"time"
)

func loadoutFixture(t *testing.T) (*Session, time.Time) {
	s := movementFixture(t, 0).session
	now := time.Unix(100, 0)
	s.phase, s.deadline = Playing, now.Add(time.Minute)
	s.movement.primaryWeapons = map[byte]uint16{1: 11, 2: 12}
	s.movement.secondaryWeapons = map[byte]uint16{1: 21}
	var policy LoadoutPolicy
	for field := range policy {
		policy[field][0], policy[field][1] = true, true
	}
	policy[2][0], policy[5][0], policy[2][2] = false, false, true
	s.movement.loadouts = map[string]LoadoutPolicy{"a": policy, "b": policy}
	for _, p := range s.peers {
		p.joined, p.loaded, p.sceneReady = true, true, true
		p.player.Beacon[6], p.player.Beacon[9], p.weapon = 1, 1, 11
		p.admission = p.player
	}
	return s, now
}

func TestLoadoutAdmissionReplicationAndRoundBoundary(t *testing.T) {
	for _, boundary := range []string{"loading", "countdown", "round-spawn", "living"} {
		t.Run(boundary, func(t *testing.T) {
			s, now := loadoutFixture(t)
			p := s.peers[0]
			if boundary == "loading" {
				s.phase = Loading
				p.sceneReady = false
			}
			if boundary == "round-spawn" || boundary == "living" {
				s.round.runningSince = now
				p.roundSpawnPending = boundary == "round-spawn"
			}
			v := game.Loadout{Actor: 0, Selections: [9]byte{2: 2, 5: 1}, Jetpack: true}
			body, _ := v.Message()
			out, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true}, body), now)
			if err != nil {
				t.Fatal(err)
			}
			got, _ := p.player.Loadout()
			if boundary == "living" {
				if got == v || p.weapon != 11 {
					t.Fatal("living actor reset inventory")
				}
				return
			}
			if got != v || p.weapon != 12 || s.equippedWeapon(p, 11) || !s.equippedWeapon(p, 12) {
				t.Fatal("loadout did not update admission", got)
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
				for _, message := range packet.Messages {
					if bytes.Equal(message, body) {
						found = packet.Header.Reliable
					}
				}
			}
			if found != (boundary != "loading") {
				t.Fatal("wrong loadout audience", out)
			}
			// Reliable duplicate must not apply, change state or create a new relay.
			out, err = s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true}, body), now)
			if err != nil {
				t.Fatal(err)
			}
			for _, d := range out {
				if d.To != p.member.Endpoint {
					t.Fatal("duplicate relayed")
				}
			}
		})
	}
}

func TestLoadoutRejectsForeignUnownedAndUnreliableWithoutMutation(t *testing.T) {
	for _, kind := range []string{"foreign", "unowned", "unreliable", "mixed-invalid"} {
		t.Run(kind, func(t *testing.T) {
			s, now := loadoutFixture(t)
			p := s.peers[0]
			before := p.player
			v := game.Loadout{Selections: [9]byte{2: 2, 5: 1}}
			if kind == "foreign" {
				v.Actor = 1
			}
			if kind == "unowned" {
				v.Selections[3] = 2
			}
			body, _ := v.Message()
			if kind == "mixed-invalid" {
				body = append(body, game.AimWeapon, 1)
			}
			_, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: kind != "unreliable"}, body), now)
			if !errors.Is(err, ErrPeer) && !errors.Is(err, ErrState) {
				t.Fatal(err)
			}
			if p.player != before || p.weapon != 11 {
				t.Fatal("rejected datagram changed equipment")
			}
		})
	}
}

func TestRoundLoadoutPrecedesSpawnAndOriginalAdmissionRemainsRetryable(t *testing.T) {
	s, now := loadoutFixture(t)
	p := s.peers[0]
	p.discovered, p.roundSpawnPending = true, true
	s.round.runningSince = now
	initial, _ := p.admission.Message()
	if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: 1}, initial), now); err != nil {
		t.Fatal(err)
	}
	v := game.Loadout{Selections: [9]byte{2: 2, 5: 1}}
	body, _ := v.Message()
	body = append(body, game.RequestRespawn, 0)
	out, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: 2}, body), now)
	if err != nil || p.weapon != 12 || p.roundSpawnPending {
		t.Fatal("round equipment/spawn", err)
	}
	var remote []byte
	for _, d := range out {
		if d.To != s.peers[1].member.Endpoint {
			continue
		}
		packet, err := game.Decode(d.Data)
		if err != nil {
			t.Fatal(err)
		}
		for _, m := range packet.Messages {
			remote = append(remote, m[0])
		}
	}
	if !bytes.Equal(remote, []byte{game.PlayerLoadout, game.ChangeWeapon, game.AimWeapon, game.RespawnPlayer}) {
		t.Fatal("equipment must precede spawn", remote)
	}
	// A retransmitted initial identity packet carries its original loadout.
	// It must be ACKed without overwriting the newer per-match selection.
	_, err = s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: 1}, initial), now)
	if err != nil || p.weapon != 12 || p.player.Beacon[6] != 2 {
		t.Fatal("initial admission retry", err)
	}
}

func TestLoadoutAndWeaponShareDatagramAndOlderSelectionCannotRollback(t *testing.T) {
	s, now := loadoutFixture(t)
	p := s.peers[0]
	v := game.Loadout{Selections: [9]byte{2: 2, 5: 1}}
	body, _ := v.Message()
	weapon, _ := (game.WeaponAction{Value: 12}).Message(game.ChangeWeapon)
	if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: 10}, body, weapon), now); err != nil {
		t.Fatal("combined loadout/weapon", err)
	}
	v.Selections[2] = 1
	older, _ := v.Message()
	oldWeapon, _ := (game.WeaponAction{Value: 11}).Message(game.ChangeWeapon)
	if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: 9}, older, oldWeapon, []byte{game.AimWeapon, 0x80}), now); err != nil {
		t.Fatal(err)
	}
	if p.weapon != 12 || p.player.Beacon[6] != 2 || p.aiming {
		t.Fatal("reordered loadout rolled back equipment")
	}
	before := len(p.channel.pending)
	if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: 10}, older), now); !errors.Is(err, game.ErrPacket) {
		t.Fatal("changed reliable body admitted", err)
	}
	if len(p.channel.pending) != before || p.player.Beacon[6] != 2 {
		t.Fatal("rejected reuse mutated state")
	}
}

func TestAcceptedReliableBodySurvivesPhaseChangeAndHistoryIsBounded(t *testing.T) {
	s, now := loadoutFixture(t)
	p := s.peers[0]
	s.phase = Loading
	finished := wire(t, game.Header{Reliable: true}, []byte{game.FinishedLoading})
	if _, err := s.Handle(p.member.Endpoint, finished, now); err != nil {
		t.Fatal(err)
	}
	s.phase = Playing
	out, err := s.Handle(p.member.Endpoint, finished, now)
	if err != nil || len(out) != 1 || out[0].Data[game.HeaderSize] != game.Acknowledge {
		t.Fatal("phase retry not ACKed", err)
	}
	// The body cache does not bypass header/ACK validation.
	invalidAck := wire(t, game.Header{Reliable: true, Ack: 1000, AckMask: 1}, []byte{game.FinishedLoading})
	if _, err := s.Handle(p.member.Endpoint, invalidAck, now); !errors.Is(err, game.ErrPacket) {
		t.Fatal("invalid retry ACK admitted", err)
	}
	for sequence := uint32(1); sequence <= 300; sequence++ {
		if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: sequence}, []byte{game.Acknowledge}), now); err != nil {
			t.Fatal(sequence, err)
		}
	}
	if len(p.receivedBodies) != 256 || len(p.channel.seen) != 256 {
		t.Fatal("unbounded reliable history", len(p.receivedBodies))
	}
}
