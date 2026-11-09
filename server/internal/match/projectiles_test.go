package match

import (
	"fmt"
	"nova3.test/backend/internal/authority"
	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/world"
	"os"
	"testing"
	"time"
)

// The connected check consumes only the distributable numeric definitions;
// no original library, archive, graphics or native oracle is required.
func connectedCombatFixture(t *testing.T) (*Session, time.Time) {
	t.Helper()
	s, now := inventoryFixture(t)
	data, err := os.ReadFile("../../data/authority.json")
	if err != nil {
		t.Fatal(err)
	}
	s.authority, err = authority.Decode(data)
	if err != nil {
		t.Fatal(err)
	}
	s.epoch = now.Add(-time.Second)
	s.movement.bounds = world.PositionBounds{Minimum: world.Vec3{X: -20, Y: -20, Z: -20}, Maximum: world.Vec3{X: 20, Y: 20, Z: 20}}
	s.movement.scene, _ = world.NewScene(nil)
	s.movement.primaryWeapons = map[byte]uint16{1: 23085, 2: 23085}
	s.movement.secondaryWeapons = map[byte]uint16{1: 23072}
	for i, p := range s.peers {
		p.weapon = 23085
		if err = s.resetInventory(p); err != nil {
			t.Fatal(err)
		}
		s.resetLife(p, now)
		s.movement.poses[i].Position = [3]float32{0, float32(i * 10), 0}
	}
	return s, now
}

func TestFirstWeaponConnectedCombat(t *testing.T) {
	s, now := connectedCombatFixture(t)
	attacker, victim := s.peers[0], s.peers[1]
	var lastPacket []byte
	origin := [3]uint16{32768, 32768, 35225} // Synthetic bounds encode(0,0,1.5).
	for shot := range 20 {
		at := now.Add(time.Duration(shot) * 250 * time.Millisecond)
		v := game.Shot{Time: s.elapsed(at), Actor: attacker.player.Slot, Weapon: attacker.weapons[23085].definition.WireIndex,
			Origin: origin, Direction: [3]uint16{32767, 65535, 32767}, SourceType: 3}
		message, _ := v.Message()
		if shot == 0 {
			message = append([]byte{game.RequestSelfDamage, attacker.player.Slot, 0x42, 0xc6, 0, 0}, message...)
		}
		lastPacket = wire(t, game.Header{Reliable: true, Sequence: uint32(shot)}, message)
		if _, err := s.Handle(attacker.member.Endpoint, lastPacket, at); err != nil {
			t.Fatal(err)
		}
		if _, err := s.Tick(at.Add(33 * time.Millisecond)); err != nil {
			t.Fatal(err)
		}
		if victim.life.dead {
			break
		}
	}
	if !victim.life.dead || attacker.life.kills != 1 || victim.life.deaths != 1 || attacker.life.score == 0 {
		t.Fatal("connected combat did not produce death/score", attacker.life, victim.life)
	}
	if attacker.life.health != s.authority.Player().BaseHealth {
		t.Fatal("client self-damage claim granted damage")
	}
	score := attacker.life.score
	if _, err := s.Handle(attacker.member.Endpoint, lastPacket, now.Add(5*time.Second)); err != nil {
		t.Fatal(err)
	}
	if attacker.life.kills != 1 || attacker.life.score != score || victim.life.deaths != 1 {
		t.Fatal("replay repeated death/score")
	}
	var out []Delivery
	if err := s.spawnRound(victim, now.Add(6*time.Second), &out); err != nil {
		t.Fatal(err)
	}
	if victim.life.dead || victim.life.health != s.authority.Player().BaseHealth || len(out) == 0 {
		t.Fatal("connected respawn failed", victim.life)
	}
}

func TestFirstWeaponWallStopsDamage(t *testing.T) {
	s, now := connectedCombatFixture(t)
	wall := world.Object{ID: 1, Static: true, Enabled: true, Transform: world.Transform{Scale: world.Vec3{X: 1, Y: 1, Z: 1}}, Parts: []world.Part{{Filter: world.Filter{Group: 2, Mask: 0xffff}, Surfaces: []world.Surface{{A: world.Vec3{X: -5, Y: 5, Z: -5}, B: world.Vec3{X: 5, Y: 5, Z: -5}, C: world.Vec3{X: 0, Y: 5, Z: 5}, Mask: world.All}}}}}
	s.movement.scene, _ = world.NewScene([]world.Object{wall})
	p, victim := s.peers[0], s.peers[1]
	shot := game.Shot{Time: s.elapsed(now), Actor: p.player.Slot, Weapon: p.weapons[23085].definition.WireIndex,
		Origin: [3]uint16{32768, 32768, 35225}, Direction: [3]uint16{32767, 65535, 32767}, SourceType: 3}
	message, _ := shot.Message()
	if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true}, message), now); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Tick(now.Add(33 * time.Millisecond)); err != nil {
		t.Fatal(err)
	}
	if victim.life.health != s.authority.Player().BaseHealth || p.life.hits != 0 || len(s.bullets) != 0 {
		t.Fatal("wall did not occlude impact", victim.life, p.life.hits, len(s.bullets))
	}
}

func TestFirstWeaponHitsOtherTargetPoses(t *testing.T) {
	for _, held := range []uint16{23091, 23088} {
		s, now := connectedCombatFixture(t)
		p, victim := s.peers[0], s.peers[1]
		victim.weapon = held
		if s.collider(victim, now) == nil {
			t.Fatalf("missing collision pose for target holding%d", held)
		}
		shot := game.Shot{Time: s.elapsed(now), Actor: p.player.Slot, Weapon: p.weapons[23085].definition.WireIndex,
			Origin: [3]uint16{32768, 32768, 35225}, Direction: [3]uint16{32767, 65535, 32767}, SourceType: 3}
		message, _ := shot.Message()
		if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true}, message), now); err != nil {
			t.Fatal(err)
		}
		if _, err := s.Tick(now.Add(33 * time.Millisecond)); err != nil {
			t.Fatal(err)
		}
		if victim.life.health >= s.authority.Player().BaseHealth || p.life.hits != 1 {
			t.Fatalf("assault rifle did not damage target holding%d: health%v hits%d", held, victim.life.health, p.life.hits)
		}
	}
}

func equipCombatWeapon(t *testing.T, s *Session, id uint16) {
	t.Helper()
	s.movement.primaryWeapons[1] = id
	s.peers[0].weapon = id
	if err := s.resetInventory(s.peers[0]); err != nil {
		t.Fatal(err)
	}
}

func fireCombatWeapon(t *testing.T, s *Session, now time.Time) {
	t.Helper()
	p := s.peers[0]
	shot := game.Shot{Time: s.elapsed(now), Actor: p.player.Slot, Weapon: p.weapons[p.weapon].definition.WireIndex,
		Origin: [3]uint16{32768, 32768, 35225}, Direction: [3]uint16{32767, 65535, 32767}, SourceType: 3}
	message, _ := shot.Message()
	if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true}, message), now); err != nil {
		t.Fatal(err)
	}
}

func TestLaserElectricAndRocketConnectedDamage(t *testing.T) {
	for _, id := range []uint16{23094, 23088, 23091} {
		t.Run(fmt.Sprint(id), func(t *testing.T) {
			s, now := connectedCombatFixture(t)
			equipCombatWeapon(t, s, id)
			fireCombatWeapon(t, s, now)
			if _, err := s.Tick(now.Add(time.Millisecond)); err != nil {
				t.Fatal(err)
			}
			victim := s.peers[1]
			if id != 23088 && victim.life.health != s.authority.Player().BaseHealth {
				t.Fatal("finite projectile hit before travelling to target")
			}
			if _, err := s.Tick(now.Add(250 * time.Millisecond)); err != nil {
				t.Fatal(err)
			}
			if victim.life.health >= s.authority.Player().BaseHealth || s.peers[0].life.hits != 1 || len(s.bullets) != 0 {
				t.Fatal("weapon did not damage target", victim.life, s.peers[0].life, len(s.bullets))
			}
			if id == 23091 && (!victim.life.dead || s.peers[0].life.kills != 1) {
				t.Fatal("direct rocket explosion did not kill", victim.life)
			}
		})
	}
}

func TestRocketWallExplosionDamagesNearbyTarget(t *testing.T) {
	for _, distance := range []float32{10, 15} {
		s, now := connectedCombatFixture(t)
		equipCombatWeapon(t, s, 23091)
		s.movement.poses[1].Position[1] = distance
		wall := world.Object{ID: 1, Static: true, Enabled: true, Transform: world.Transform{Scale: world.Vec3{X: 1, Y: 1, Z: 1}}, Parts: []world.Part{{Filter: world.Filter{Group: 2, Mask: 0xffff}, Surfaces: []world.Surface{{A: world.Vec3{X: -5, Y: 5, Z: -5}, B: world.Vec3{X: 5, Y: 5, Z: -5}, C: world.Vec3{X: 0, Y: 5, Z: 5}, Mask: world.All}}}}}
		s.movement.scene, _ = world.NewScene([]world.Object{wall})
		fireCombatWeapon(t, s, now)
		if _, err := s.Tick(now.Add(250 * time.Millisecond)); err != nil {
			t.Fatal(err)
		}
		damaged := s.peers[1].life.health < s.authority.Player().BaseHealth
		if damaged != (distance == 10) || len(s.bullets) != 0 {
			t.Fatal("rocket explosion radius or wall collision", distance, s.peers[1].life, len(s.bullets))
		}
	}
}
