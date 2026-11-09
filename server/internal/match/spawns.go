package match

import (
	"crypto/rand"
	"math"
	"math/big"

	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/world"
)

func spawnIndex(n int) (int, error) {
	if n < 1 {
		return 0, ErrState
	}
	i, err := rand.Int(rand.Reader, big.NewInt(int64(n)))
	if err != nil {
		return 0, err
	}
	return int(i.Int64()), nil
}

// Native GetFirstSpawnPointForPlayer delegates to GetRandomSpawnPoint too.
func (s *Session) prepareSpawn(p *peer) error {
	spawn, err := s.chooseRespawn(p)
	if err != nil {
		return err
	}
	s.movement.spawns[p.player.Slot] = spawn
	p.haveSpawn = true
	pose := &s.movement.poses[p.player.Slot]
	pose.Position = [3]float32{float32(spawn.Transform.Position.X), float32(spawn.Transform.Position.Y), float32(spawn.Transform.Position.Z)}
	pose.Yaw, _ = pc.EncodeAngle(spawn.Transform.Rotation.Z)
	pose.Pitch = 0
	return nil
}

func (s *Session) spawnTeamAllowed(p *peer, spawn world.SpawnPoint) bool {
	switch s.loading.Mode {
	case CaptureTheFlag, CaptureThePoint:
		return spawn.Team == int32(p.player.Beacon[0])
	case TeamInstagib:
		return true // Native mode6 has no team0 restriction.
	default:
		return spawn.Team == 0
	}
}

func (s *Session) spawnBlocked(position world.Vec3) bool {
	if s.authority == nil {
		return false
	}
	radius := float64(s.authority.Player().Body.Radius)
	for _, v := range s.mounts {
		if v.health <= 0 {
			continue
		}
		d := position.Sub(v.position)
		switch v.definition.Category {
		case 1:
			r := radius + float64(v.definition.Radius)
			if d.Dot(d) < r*r {
				return true
			}
		case 2:
			// Native tests the live mech's actor capsule, separately from the
			// animated projectile regions required for weapon collision.
			d.Z -= max(0, min(float64(v.definition.Height), d.Z))
			r := radius + float64(v.definition.Radius)
			if d.Dot(d) < r*r {
				return true
			}
		}
	}
	return false
}

func (s *Session) spawnPeerActive(p *peer) bool {
	// Reserve selected initial grants while concurrent scenes finish loading.
	return !p.departed && !p.transportFailed && (p.sceneReady || (p.joined && p.haveSpawn))
}

func (s *Session) chooseRespawn(p *peer) (world.SpawnPoint, error) {
	if s.movement == nil || len(s.movement.spawnPool) == 0 || s.objectives == nil {
		return world.SpawnPoint{}, ErrState
	}
	rules := s.objectives.data.Rules
	clearance := float64(0)
	if s.authority != nil {
		clearance = float64(s.authority.Player().FFASpawnClearance)
	}
	if s.loading.Mode == CaptureTheFlag || s.loading.Mode == CaptureThePoint {
		clearance = rules.SpawnObjectiveClearance
	}
	previous := uint32(0)
	if p.haveSpawn {
		previous = s.movement.spawns[p.player.Slot].ID
	}
	var candidates []world.SpawnPoint
	for _, spawn := range s.movement.spawnPool {
		if !spawn.Enabled || !s.spawnTeamAllowed(p, spawn) || spawn.ID == previous {
			continue
		}
		available := !s.spawnBlocked(spawn.Transform.Position)
		for _, other := range s.peers {
			if !available {
				break
			}
			if other == p || !s.spawnPeerActive(other) {
				continue
			}
			limit := clearance
			if s.teammates(p, other) {
				limit = rules.SpawnTeammateClearance
			}
			d := spawn.Transform.Position.Sub(s.actorPosition(other))
			available = d.Dot(d) >= limit*limit
		}
		if available {
			candidates = append(candidates, spawn)
		}
	}
	if len(candidates) == 0 {
		// Emergency scan in original zone order: after twelve records, take
		// at most the first point of each remaining zone, relaxing the filters.
		var lastZone int32 = -1
		for _, spawn := range s.movement.spawnPool {
			zone := int32(0)
			if s.authority != nil {
				if z, ok := s.authority.SpawnZone(s.loading.Map, spawn.ID); ok {
					zone = z
				}
			}
			if len(candidates) >= rules.SpawnFallbackCount && zone == lastZone {
				continue
			}
			candidates = append(candidates, spawn)
			lastZone = zone
		}
	}
	i, err := spawnIndex(len(candidates))
	if err != nil {
		return world.SpawnPoint{}, err
	}
	if teamMode(s.loading.Mode) && s.loading.Mode != CaptureTheFlag {
		// Choose a random active teammate, then its nearest candidate. Keeping
		// repeated nearest points preserves the original per-teammate weighting.
		var near []world.SpawnPoint
		for _, other := range s.peers {
			if other == p || !s.spawnPeerActive(other) || !s.teammates(p, other) {
				continue
			}
			best, distance := candidates[0], math.Inf(1)
			for _, spawn := range candidates {
				d := spawn.Transform.Position.Sub(s.actorPosition(other))
				if n := d.Dot(d); n < distance {
					best, distance = spawn, n
				}
			}
			near = append(near, best)
		}
		if len(near) > 0 {
			i, err = spawnIndex(len(near))
			if err != nil {
				return world.SpawnPoint{}, err
			}
			return near[i], nil
		}
	}
	return candidates[i], nil
}
