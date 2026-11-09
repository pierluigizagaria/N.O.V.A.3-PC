package match

import (
	"encoding/binary"
	"testing"
	"time"

	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/world"
)

func TestDeathChoosesNewFFASpawnAndPublishesIt(t *testing.T) {
	s, now, _ := shotFixture(t)
	spawns := []world.SpawnPoint{
		{ID: 100, Transform: world.Transform{Position: world.Vec3{X: 80}}},
		{ID: 101, Transform: world.Transform{Position: world.Vec3{X: 1}}},
		{ID: 102, Transform: world.Transform{Position: world.Vec3{X: 50}}},
	}
	s.movement.spawnPool = spawns
	s.movement.spawns[1] = spawns[0]
	s.movement.poses[0].Position = [3]float32{}
	p := s.peers[1]
	p.life.dead = true
	var out []Delivery
	if err := s.spawnRound(p, now, &out); err != nil {
		t.Fatal(err)
	}
	if s.movement.spawns[1].ID != 102 || s.movement.poses[1].Position[0] != 50 {
		t.Fatal("respawn reused previous/nearby point", s.movement.spawns[1])
	}
	for _, d := range out {
		packet, _ := game.Decode(d.Data)
		r, err := game.ReadRespawn(packet.Messages[0])
		if err != nil || r.Spawn != 102 {
			t.Fatal("spawn publication disagrees with authority", r, err)
		}
	}
	p.life.dead = true
	if err := s.spawnRound(p, now.Add(time.Second), &out); err != nil || s.movement.spawns[1].ID != 100 {
		t.Fatal("next death reused same spawn", err)
	}
	// No eligible alternative must still permit a new life on crowded maps.
	s.movement.spawnPool = spawns[:1]
	p.life.dead = true
	if err := s.spawnRound(p, now.Add(2*time.Second), &out); err != nil || p.life.dead {
		t.Fatal("exhausted spawn pool stranded dead actor", err)
	}
}

func TestDamageDeathRespawnAndStreakScore(t *testing.T) {
	s, now, shot := shotFixture(t)
	a, v := s.peers[0], s.peers[1]
	for _, p := range s.peers {
		s.resetLife(p, now)
	}
	s.loading.Parameter2 = 50 // Native FFA threshold is kills, not XP/score.
	var out []Delivery
	if err := s.damage(a, v, 100, 8, now, &out); err != nil || v.life.health != 117 {
		t.Fatal(v.life, err)
	}
	packet, _ := game.Decode(out[0].Data)
	d, _ := game.ReadDamage(packet.Messages[0])
	if d.Amount != 8 || d.Flags != 0 {
		t.Fatal("native fractional8 wrap", d)
	}
	for i := 1; i <= 3; i++ {
		out = nil
		if err := s.damage(a, v, 100, 1000, now, &out); err != nil {
			t.Fatal(err)
		}
		if !v.life.dead || a.life.kills != uint32(i) || v.life.deaths != uint32(i) {
			t.Fatal(a.life, v.life)
		}
		before := a.life.score
		if err := s.damage(a, v, 100, 1000, now, &out); err != nil || a.life.score != before {
			t.Fatal("duplicate death counted", err)
		}
		if s.phase != Playing {
			t.Fatal("score mistaken for kills threshold")
		}
		generation := s.movement.poses[1].Generation
		now = now.Add(time.Second)
		if err := s.spawnRound(v, now, &out); err != nil {
			t.Fatal(err)
		}
		if v.life.dead || v.life.health != 125 || s.movement.poses[1].Generation != generation+1 {
			t.Fatal("respawn did not create a life")
		}
		if err := s.spawnRound(v, now, &out); err != nil || s.movement.poses[1].Generation != generation+1 {
			t.Fatal("duplicate respawn", err)
		}
	}
	if a.life.score != 7+18+27+40 {
		t.Fatal("streak/third-victim score", a.life.score)
	}
	if err := s.damage(v, a, 100, 1000, now, &out); err != nil || a.life.streak != 0 {
		t.Fatal("death retained kill streak", err)
	}
	if _, _, _, ok := s.authorizeShot(a, shot, now); ok {
		t.Fatal("dead actor fired")
	}
	if err := s.spawnRound(a, now, &out); err != nil {
		t.Fatal(err)
	}
	if _, _, _, ok := s.authorizeShot(a, shot, now); ok {
		t.Fatal("old-life shot crossed respawn")
	}
}

func TestAuthoritativeKillLimitEndsAfterDeath(t *testing.T) {
	s, now, _ := shotFixture(t)
	for _, p := range s.peers {
		s.resetLife(p, now)
	}
	s.loading.Parameter2 = 1
	var out []Delivery
	if err := s.damage(s.peers[0], s.peers[1], 100, 125, now, &out); err != nil || s.phase != Finishing {
		t.Fatal(err, s.phase)
	}
	var own []byte
	for _, d := range out {
		if d.To != s.peers[1].member.Endpoint {
			continue
		}
		p, _ := game.Decode(d.Data)
		for _, m := range p.Messages {
			own = append(own, m[0])
		}
	}
	if len(own) != 3 || own[0] != game.DamagePlayer || own[1] != game.PlayerDeath || own[2] != game.EndGame {
		t.Fatal("end before death", own)
	}
}

func TestLateJoinSnapshotPreservesDeathAndScore(t *testing.T) {
	s, now, _ := shotFixture(t)
	old, newcomer := s.peers[0], s.peers[1]
	old.life = lifeState{dead: true, kills: 3, deaths: 2, score: 92}
	newcomer.sceneReady = false
	var out []Delivery
	if err := s.activate(newcomer, now, &out); err != nil {
		t.Fatal(err)
	}
	for _, d := range out {
		if d.To != newcomer.member.Endpoint {
			continue
		}
		p, _ := game.Decode(d.Data)
		for _, m := range p.Messages {
			if m[0] != game.PlayersSnapshot {
				continue
			}
			r := m[25:88]
			if binary.BigEndian.Uint16(r[40:]) != 3 || binary.BigEndian.Uint16(r[42:]) != 2 || binary.BigEndian.Uint32(r[46:]) != 92 || r[56]&1 == 0 {
				t.Fatal("late join lost state")
			}
			return
		}
	}
	t.Fatal("no initial snapshot")
}
