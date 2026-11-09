package match

import (
	"nova3.test/backend/internal/protocol/game"
	"time"
)

// LoadoutPolicy is frozen from catalogue ownership when the room is reserved.
// It grants selections, not ammunition, damage, fuel or combat modifiers.
type LoadoutPolicy [9][128]bool

func (policy LoadoutPolicy) Allows(v game.Loadout) bool {
	for field, selection := range v.Selections {
		if selection >= 128 || !policy[field][selection] {
			return false
		}
	}
	return true
}

func (s *Session) validLoadout(p *peer, v game.Loadout) bool {
	if s.movement == nil {
		return true
	}
	if s.authority != nil {
		player := p.player
		player.SetLoadout(v)
		if _, err := s.makeInventory(player); err != nil {
			return false
		}
	}
	if len(s.movement.primaryWeapons) != 0 && s.movement.primaryWeapons[v.Selections[2]] == 0 {
		return false
	}
	if len(s.movement.secondaryWeapons) != 0 && s.movement.secondaryWeapons[v.Selections[5]] == 0 {
		return false
	}
	if s.movement.loadouts != nil {
		policy, ok := s.movement.loadouts[p.member.Account]
		return ok && policy.Allows(v)
	}
	return true
}

func (s *Session) loadoutBoundary(p *peer) bool {
	return s.phase != Playing || !p.sceneReady || s.round.runningSince.IsZero() || p.roundSpawnPending || p.life.dead
}

func (s *Session) applyLoadout(p *peer, v game.Loadout, sequence uint32, now time.Time, out *[]Delivery) error {
	if p.haveLoadoutSequence && sequence < p.loadoutSequence {
		return nil
	}
	p.loadoutSequence, p.haveLoadoutSequence = sequence, true
	old, _ := p.player.Loadout()
	if old == v {
		return nil
	}
	// A living actor cannot use equipment messages to reset its inventory.
	// Drain late requests reliably, as for repeated spawn requests. Loading,
	// waiting/countdown and the server-issued round spawn are valid boundaries.
	if !s.loadoutBoundary(p) {
		return nil
	}
	p.player.SetLoadout(v)
	p.weapon = s.movement.primaryWeapons[v.Selections[2]]
	p.aiming = false
	body, _ := v.Message()
	if p.weapon != 0 {
		weapon, _ := (game.WeaponAction{Actor: v.Actor, Value: int32(p.weapon)}).Message(game.ChangeWeapon)
		body = append(body, weapon...)
	}
	aim, _ := (game.Aim{Actor: v.Actor}).Message()
	body = append(body, aim...)
	return s.present(p, body, true, now, out)
}
