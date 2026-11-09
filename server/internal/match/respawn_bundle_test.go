package match

import (
	"testing"
	"time"

	"nova3.test/backend/internal/protocol/game"
)

func TestRepeatedRespawnWithPowerAndMineIntents(t *testing.T) {
	s, now, _ := shotFixture(t)
	a, v := s.peers[0], s.peers[1]
	s.resetLife(a, now)
	s.resetLife(v, now)
	for cycle := uint32(0); cycle < 12; cycle++ {
		var out []Delivery
		if err := s.damage(a, v, 100, 1000, now, &out); err != nil {
			t.Fatal(err)
		}
		generation := s.movement.poses[1].Generation
		now = now.Add(6 * time.Second)
		loadout, _ := v.player.Loadout()
		equipment, _ := loadout.Message()
		// Original dead-state update queues loadout33 then request32 once.
		// A pending power49/mine51 record must not reject that whole bundle.
		packet := wire(t, game.Header{Reliable: true, Sequence: cycle},
			[]byte{49, 1, 2}, []byte{51, 1}, equipment, []byte{game.RequestRespawn, 1})
		out, err := s.Handle(v.member.Endpoint, packet, now)
		if err != nil || v.life.dead || v.life.health != 125 || s.movement.poses[1].Generation != generation+1 {
			t.Fatalf("cycle %d stranded in death: %v life=%+v", cycle, err, v.life)
		}
		spawns := 0
		for _, delivery := range out {
			pkt, err := game.Decode(delivery.Data)
			if err != nil {
				t.Fatal(err)
			}
			for _, m := range pkt.Messages {
				if m[0] == game.RespawnPlayer {
					spawns++
				}
				if m[0] == 49 || m[0] == 51 {
					t.Fatal("unimplemented power/mine relayed as authority")
				}
			}
		}
		if spawns != 2 || a.life.kills != cycle+1 || v.life.deaths != cycle+1 {
			t.Fatal("missing respawn publication or duplicate score", spawns, a.life, v.life)
		}
		if _, err = s.Handle(v.member.Endpoint, packet, now); err != nil || s.movement.poses[1].Generation != generation+1 {
			t.Fatal("retry changed life", err)
		}
		for _, p := range s.peers {
			ack := game.Header{Ack: p.channel.next - 1, AckMask: 0xffffffff}
			if _, err = s.Handle(p.member.Endpoint, wire(t, ack, []byte{game.Acknowledge}), now); err != nil {
				t.Fatal(err)
			}
		}
	}
}

func TestPowerAndMineRejectForeignOrUnreliableBundles(t *testing.T) {
	for _, body := range [][]byte{{49, 1, 2}, {51, 1}, {49, 0, 2}, {51, 0}, {127, 1, 0, 0, 0, 1}, {127, 0, 0, 0, 0, 1}, {129, 1}, {129, 0}} {
		s, now := loadoutFixture(t)
		p := s.peers[0]
		_, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: body[1] == 1}, body), now)
		if err == nil || p.channel.haveReceived {
			t.Fatal("invalid intent acknowledged", body, err)
		}
	}
}

func TestMachinegunIntentCannotBlockOnFootDamage(t *testing.T) {
	s, now := connectedCombatFixture(t)
	p, victim := s.peers[0], s.peers[1]
	shot := game.Shot{Time: s.elapsed(now), Actor: 0, Weapon: p.weapons[23085].definition.WireIndex,
		Origin: [3]uint16{32768, 32768, 35225}, Direction: [3]uint16{32767, 65535, 32767}, SourceType: 3}
	message, _ := shot.Message()
	packet := wire(t, game.Header{Reliable: true}, []byte{127, 0, 0, 0, 0, 1}, message, []byte{129, 0})
	if _, err := s.Handle(p.member.Endpoint, packet, now); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Tick(now.Add(33 * time.Millisecond)); err != nil {
		t.Fatal(err)
	}
	if victim.life.health >= s.authority.Player().BaseHealth || p.life.hits != 1 {
		t.Fatal("machinegun request stranded an on-foot shot", victim.life, p.life)
	}
	health := victim.life.health
	if _, err := s.Handle(p.member.Endpoint, packet, now.Add(time.Second)); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Tick(now.Add(time.Second)); err != nil {
		t.Fatal(err)
	}
	if health != victim.life.health || p.life.hits != 1 {
		t.Fatal("retry reapplied damage")
	}
}
