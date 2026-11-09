package match

import (
	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/protocol/pc"
	"time"
)

// SceneReady is sent after the native client enters the playable scene. The
// loading roster alone does not activate remote player objects. Existing peers
// receive NewPlayer; the newcomer receives the complete initial snapshot. Both
// native readers add roster entries, so sending both to one recipient duplicates
// actors in its fixed twelve-record roster.
func (s *Session) activate(p *peer, now time.Time, out *[]Delivery) error {
	if p.sceneReady {
		return nil
	}
	if err := s.resetInventory(p); err != nil {
		return err
	}
	p.sceneReady = true
	s.resetLife(p, now)
	s.collider(p, now)
	s.recordHitFrame(now)
	p.admissionDeadline = time.Time{}
	message := func(peer *peer) ([]byte, error) {
		return (game.Presence{Player: peer.player, Spawn: s.movement.spawns[peer.player.Slot].ID}).Message()
	}
	newcomer, err := message(p)
	if err != nil {
		return err
	}
	if p.zone != nil {
		zone, _ := p.zone.Message()
		newcomer = append(newcomer, zone...)
	}
	var existing []byte
	for _, other := range s.peers {
		if other == p || !other.sceneReady {
			continue
		}
		if err = s.emit(other, newcomer, true, now, out); err != nil {
			return err
		}
	}
	initial := game.InitialSnapshot{RenderDelayMS: pc.PresentationDelayMS,
		TeamScores: [2]uint16{uint16(max(0, min(s.teamScores[0], 65535))), uint16(max(0, min(s.teamScores[1], 65535)))}}
	if s.loading.Mode == CaptureTheFlag {
		for i, f := range s.objectives.flags {
			position, e := s.movement.bounds.EncodePosition(f.position)
			if e != nil {
				return e
			}
			initial.FlagPositions[i], initial.FlagObjects[i] = position, uint16(f.definition.Object)
		}
	}
	for _, other := range s.peers {
		if other.sceneReady {
			initial.Players = append(initial.Players, game.InitialPlayer{Player: other.player, Active: true,
				Weapon: other.weapon, Dead: other.life.dead, Frozen: other.life.frozen, Kills: uint16(min(other.life.kills, 65535)), Deaths: uint16(min(other.life.deaths, 65535)), Score: other.life.score,
				Flag:              uint16(min(other.life.flags, 65535)),
				AuxiliaryCounters: [3]uint16{uint16(min(other.life.streak, 65535)), uint16(min(other.life.freezeStreak, 65535)), uint16(min(other.life.thawStreak, 65535))}})
		}
	}
	state, err := initial.Message()
	if err != nil {
		return err
	}
	// Complete the original post-load request even for the first/only player.
	// The snapshot itself admits existing actors, including player slot zero;
	// poses remain on the separate authenticated 30 Hz stream.
	existing = append(existing, state...)
	objectiveScene, e := s.objectiveScene()
	if e != nil {
		return e
	}
	existing = append(existing, objectiveScene...)
	for _, other := range s.peers {
		if other != p && other.sceneReady && other.zone != nil {
			zone, _ := other.zone.Message()
			existing = append(existing, zone...)
		}
		if other != p && other.sceneReady && other.aiming {
			aim, _ := (game.Aim{Actor: other.player.Slot, Enabled: true}).Message()
			existing = append(existing, aim...)
		}
	}
	if err = s.emit(p, existing, true, now, out); err != nil {
		return err
	}
	// Mount state follows roster admission so every occupied actor exists
	// before its native seat grant is consumed by a late joiner.
	if mounted := s.mountedScene(p, now); len(mounted) != 0 {
		return s.emit(p, mounted, true, now, out)
	}
	return nil
}

func (s *Session) equippedWeapon(p *peer, id uint16) bool {
	return p.weapons[id] != nil || s.playerHasWeapon(p.player, id)
}

func (s *Session) playerHasWeapon(p game.Player, id uint16) bool {
	return id != 0 && (id == s.movement.primaryWeapons[p.Beacon[6]] || id == s.movement.secondaryWeapons[p.Beacon[9]])
}

func (s *Session) present(p *peer, message []byte, reliable bool, now time.Time, out *[]Delivery) error {
	if !p.sceneReady {
		return nil
	}
	for _, other := range s.peers {
		if other != p && other.sceneReady {
			if err := s.emit(other, message, reliable, now, out); err != nil {
				return err
			}
		}
	}
	return nil
}
