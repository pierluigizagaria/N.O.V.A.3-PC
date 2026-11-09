package match

import (
	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/world"
	"testing"
	"time"
)

func shotFixture(t *testing.T) (*Session, time.Time, game.Shot) {
	s, now := inventoryFixture(t)
	s.epoch = now.Add(-time.Second)
	s.movement.bounds = world.PositionBounds{Minimum: world.Vec3{}, Maximum: world.Vec3{X: 100, Y: 100, Z: 100}}
	s.movement.scene, _ = world.NewScene(nil)
	s.movement.poses[0].Position = [3]float32{}
	return s, now, game.Shot{Time: 1000, Actor: 0, Weapon: 42, Origin: [3]uint16{0, 0, 655}, Direction: [3]uint16{65535, 32767, 32767}, SourceType: 3}
}

func TestShotAdmissionSpendsOnceAndPublishes(t *testing.T) {
	s, now, v := shotFixture(t)
	p := s.peers[0]
	body, _ := v.Message()
	packet := wire(t, game.Header{Reliable: true}, body)
	out, err := s.Handle(p.member.Endpoint, packet, now)
	if err != nil {
		t.Fatal(err)
	}
	found := false
	for _, d := range out {
		decoded, e := game.Decode(d.Data)
		if e != nil {
			t.Fatal(e)
		}
		for _, m := range decoded.Messages {
			if m[0] == game.LaunchBullet && d.To == s.peers[1].member.Endpoint {
				found = true
			}
		}
	}
	if !found || p.weapons[100].ammo.Snapshot().Magazine != 34 {
		t.Fatal("shot missing", out, p.weapons[100].ammo.Snapshot())
	}
	// Both reliable transport retry and replay with a new envelope are inert.
	for _, replay := range [][]byte{packet, wire(t, game.Header{Reliable: true, Sequence: 1}, body), wire(t, game.Header{}, body)} {
		if _, err = s.Handle(p.member.Endpoint, replay, now); err != nil {
			t.Fatal(err)
		}
	}
	if p.weapons[100].ammo.Snapshot().Magazine != 34 {
		t.Fatal("replayed discharge")
	}
}

func TestShotRejectsUnownedStaleRemoteOriginAndReload(t *testing.T) {
	for _, name := range []string{"weapon", "source", "mode", "stale", "future", "origin", "direction", "countdown", "spawn", "reload", "empty"} {
		t.Run(name, func(t *testing.T) {
			s, now, v := shotFixture(t)
			p := s.peers[0]
			w := p.weapons[100]
			switch name {
			case "weapon":
				v.Weapon = 43
			case "source":
				v.SourceType = 1
			case "mode":
				v.Flags = 1
			case "stale":
				v.Time = uint32(0xffffffff - 3000)
			case "future":
				v.Time = 4000
			case "origin":
				v.Origin[0] = 65535
			case "direction":
				v.Direction = [3]uint16{}
			case "countdown":
				s.round.runningSince = time.Time{}
			case "spawn":
				p.roundSpawnPending = true
			case "reload":
				w.ammo.Consume(now)
				w.ammo.StartReload(now)
			case "empty":
				for w.ammo.Consume(now) {
				}
			}
			before := w.ammo.Snapshot()
			if _, _, _, ok := s.authorizeShot(p, v, now); ok || w.ammo.Snapshot() != before || w.haveShot {
				t.Fatal("invalid shot spent ammo")
			}
		})
	}
}

func TestShotServerTimeBoundsBurstAndRejectsOriginAcrossWall(t *testing.T) {
	s, now, v := shotFixture(t)
	p := s.peers[0]
	for range 2 {
		if _, _, _, ok := s.authorizeShot(p, v, now); !ok {
			t.Fatal("valid batch")
		}
		v.Time++
	}
	if _, _, _, ok := s.authorizeShot(p, v, now); ok {
		t.Fatal("client clock minted firing budget")
	}
	if _, _, _, ok := s.authorizeShot(p, v, now.Add(150*time.Millisecond)); !ok {
		t.Fatal("budget did not refill")
	}
	s, now, v = shotFixture(t)
	p = s.peers[0]
	s.movement.scene, _ = world.NewScene([]world.Object{{ID: 1, Static: true, Enabled: true, Transform: world.Transform{Scale: world.Vec3{X: 1, Y: 1, Z: 1}}, Parts: []world.Part{{Filter: world.Filter{Group: 2, Mask: 0xffff}, Surfaces: []world.Surface{{A: world.Vec3{X: .5, Y: -5, Z: -5}, B: world.Vec3{X: .5, Y: 5, Z: -5}, C: world.Vec3{X: .5, Y: 0, Z: 5}, Mask: world.All}}}}}})
	v.Origin[0] = 655
	if _, _, _, ok := s.authorizeShot(p, v, now); ok {
		t.Fatal("muzzle on far side of wall")
	}
}
