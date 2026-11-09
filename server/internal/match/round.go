package match

import (
	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/protocol/pc"
	"time"
)

// Original MP_UpdateStartGame uses a ten-second round countdown after at least
// two FFA actors become active. The two-second Start18 synchronization barrier
// is a different clock and must not consume the selected match time limit.
const nativeRoundCountdown = 10 * time.Second

type roundState struct {
	countdown, runningSince time.Time
	played                  time.Duration
}

func (s *Session) roundClock(now time.Time) game.RoundClock {
	r := game.RoundClock{Running: !s.round.runningSince.IsZero(), CountingDown: !s.round.countdown.IsZero()}
	elapsed := s.round.played
	if r.Running {
		elapsed += now.Sub(s.round.runningSince)
		r.CountdownAt = s.elapsed(s.round.runningSince)
	}
	if elapsed < 0 {
		elapsed = 0
	}
	if elapsed/time.Millisecond > 0x7fffffff {
		elapsed = 0x7fffffff * time.Millisecond
	}
	r.Elapsed = uint32(elapsed / time.Millisecond)
	if r.CountingDown {
		r.CountdownAt = s.elapsed(s.round.countdown)
	}
	return r
}

func (s *Session) advanceRound(now time.Time, out *[]Delivery) error {
	if s.phase != Playing || s.movement == nil {
		return nil
	}
	active := 0
	var teams [2]bool
	for _, p := range s.peers {
		if p.sceneReady && !p.departed && !p.transportFailed {
			active++
			if team := p.player.Beacon[0]; team >= 1 && team <= 2 {
				teams[team-1] = true
			}
		}
	}
	var body []byte
	switch {
	case active < 2 || (teamMode(s.loading.Mode) && (!teams[0] || !teams[1])):
		if s.round.countdown.IsZero() && s.round.runningSince.IsZero() {
			return nil
		}
		if !s.round.runningSince.IsZero() {
			s.round.played += now.Sub(s.round.runningSince)
		}
		s.round.countdown, s.round.runningSince, s.endsAt = time.Time{}, time.Time{}, time.Time{}
		body = []byte{game.RoundWaiting}
	case !s.round.runningSince.IsZero():
		return nil
	case s.round.countdown.IsZero():
		s.round.countdown = now.Add(nativeRoundCountdown)
		body = game.RoundCountdownMessage(s.elapsed(s.round.countdown))
	case now.Before(s.round.countdown):
		return nil
	default:
		// Native round start respawns actors. Release seats before that command:
		// otherwise the client respawns its hidden foot actor while its camera
		// and the server's occupancy still belong to the old vehicle.
		for _, p := range s.peers {
			if err := s.leaveMount(p, now, out); err != nil {
				return err
			}
		}
		if err := s.resetMounts(now, out); err != nil {
			return err
		}
		if err := s.resetPickups(now, out); err != nil {
			return err
		}
		s.round.runningSince = s.round.countdown
		s.round.countdown = time.Time{}
		if s.loading.Parameter1 != 0 {
			s.endsAt = s.round.runningSince.Add(time.Duration(s.loading.Parameter1)*time.Minute - s.round.played)
		}
		body = []byte{game.RoundStarted}
		for _, p := range s.peers {
			p.roundSpawnPending = p.sceneReady && !p.departed && !p.transportFailed
		}
	}
	for _, p := range s.peers {
		if p.sceneReady {
			if err := s.emit(p, body, true, now, out); err != nil {
				return err
			}
		}
	}
	return nil
}

// RoundStarted makes the original client send loadout33 followed by request32.
// Only this server-owned transition grants a spawn; repeated client requests
// cannot teleport, replenish a life or invent a combat death.
func (s *Session) spawnRound(p *peer, now time.Time, out *[]Delivery) error {
	if (!p.roundSpawnPending && !p.life.dead) || s.round.runningSince.IsZero() {
		return nil
	}
	i := p.player.Slot
	if err := s.leaveMount(p, now, out); err != nil {
		return err
	}
	spawn, err := s.chooseRespawn(p)
	if err != nil {
		return err
	}
	if err := s.resetInventory(p); err != nil {
		return err
	}
	body, err := (game.Respawn{Time: s.elapsed(now), Slot: i, Spawn: spawn.ID}).Message()
	if err != nil {
		return err
	}
	p.roundSpawnPending, p.aiming, p.zone = false, false, nil
	s.movement.spawns[i] = spawn
	p.haveSpawn = true
	s.resetLife(p, now)
	p.respawns++
	pose := &s.movement.poses[i]
	pose.Generation++
	if pose.Generation == 0 {
		pose.Generation = 1
	}
	pose.Position = [3]float32{float32(spawn.Transform.Position.X), float32(spawn.Transform.Position.Y), float32(spawn.Transform.Position.Z)}
	pose.Yaw, _ = pc.EncodeAngle(spawn.Transform.Rotation.Z)
	pose.Pitch = 0
	pose.Time = s.elapsed(now)
	s.collider(p, now)
	s.recordHitFrame(now)
	s.movement.seen[i], s.movement.upstream[i] = false, 0
	for _, other := range s.peers {
		if other.sceneReady {
			if err = s.emit(other, body, true, now, out); err != nil {
				return err
			}
		}
	}
	return nil
}
