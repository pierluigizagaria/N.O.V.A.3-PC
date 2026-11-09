package match

import (
	"nova3.test/backend/internal/authority"
	"nova3.test/backend/internal/protocol/game"
	"time"
)

type weaponState struct {
	definition authority.Weapon
	modifiers  authority.WeaponModifiers
	ammo       *authority.Ammo
	fire       authority.FireGate
	lastShot   uint32
	haveShot   bool
	burstShots int32
	burstReady time.Time
}

func (s *Session) burstAllows(w *weaponState, mode int, now time.Time) bool {
	_, limited := s.authority.Burst(w.definition.TypeID, mode)
	return !limited || !now.Before(w.burstReady)
}
func (s *Session) commitBurst(w *weaponState, mode int, now time.Time) {
	if rule, ok := s.authority.Burst(w.definition.TypeID, mode); ok {
		w.burstShots++
		if w.burstShots >= rule.Shots {
			w.burstShots = 0
			w.burstReady = now.Add(time.Duration(rule.PauseMS) * time.Millisecond)
		}
	}
}

// makeInventory uses server-owned definitions and the already-authorized
// loadout. It cannot be called as a refill operation by a living client.
func (s *Session) makeInventory(player game.Player) (map[uint16]*weaponState, error) {
	if s.authority == nil {
		return nil, nil
	} // Isolated connection/codec fixtures.
	v, err := player.Loadout()
	if err != nil || s.movement == nil {
		return nil, ErrPeer
	}
	selected := make(map[uint16][]byte)
	for _, row := range []struct {
		field   int
		weapons map[byte]uint16
	}{{2, s.movement.primaryWeapons}, {5, s.movement.secondaryWeapons}} {
		id := row.weapons[v.Selections[row.field]]
		if id == 0 {
			return nil, ErrPeer
		}
		if _, ok := selected[id]; !ok {
			selected[id] = nil
		}
		for _, selection := range v.Selections[row.field+1 : row.field+3] {
			if selection == 0 {
				continue
			}
			addon, ok := s.movement.addons[selection]
			if !ok {
				return nil, ErrPeer
			}
			selected[id] = append(selected[id], addon)
		}
	}
	inventory := make(map[uint16]*weaponState, len(selected))
	for id, addons := range selected {
		definition, ok := s.authority.Weapon(int32(id))
		if !ok {
			return nil, ErrPeer
		}
		modifiers, err := s.authority.WeaponAddons().Modifiers(addons...)
		if err != nil {
			return nil, ErrPeer
		}
		ammo, err := authority.NewEquippedAmmo(definition, modifiers)
		if err != nil {
			return nil, ErrPeer
		}
		inventory[id] = &weaponState{definition: definition, modifiers: modifiers, ammo: ammo}
	}
	return inventory, nil
}

func (s *Session) resetInventory(p *peer) error {
	inventory, err := s.makeInventory(p.player)
	if err != nil {
		return err
	}
	p.weapons = inventory
	p.throwStock = make(map[int32]int32)
	p.throwEquipment = make(map[int32]authority.Equipment)
	p.haveThrow = false
	if s.authority != nil && s.movement != nil {
		if e, ok := s.authority.Equipment(int32(s.movement.items[p.player.Beacon[12]])); ok {
			p.throwStock[e.GrenadeTypeID] = e.Stock
			p.throwEquipment[e.GrenadeTypeID] = e
		}
	}
	return nil
}

func (s *Session) reload(p *peer, message []byte, now time.Time, out *[]Delivery) error {
	intent, _ := game.ReadPlayerState(message)
	if !p.sceneReady || p.roundSpawnPending || p.life.dead || p.life.frozen || s.round.runningSince.IsZero() {
		return nil
	}
	if intent.State != 3 {
		if intent.State <= 2 {
			p.aiming = intent.State == 2
			action := 0
			if p.aiming {
				action = 1
			}
			s.collisionAction(p, action, now)
			return s.present(p, message, true, now, out)
		}
		return nil
	}
	weapon := p.weapons[p.weapon]
	if weapon == nil || !weapon.ammo.StartReload(now) {
		return nil
	}
	action := 2
	if p.aiming {
		action = 3
	}
	s.collisionAction(p, action, now)
	return s.present(p, message, true, now, out)
}

func (p *peer) selectWeapon(id uint16, now time.Time) {
	if id == p.weapon {
		return
	}
	if old := p.weapons[p.weapon]; old != nil {
		old.ammo.CancelReload(now)
	}
	p.weapon = id
}

// Native mounted guns and equipped mines temporarily select an auxiliary
// weapon. They do not replace or refill the player's two owned weapon slots.
func (s *Session) temporaryWeapon(p *peer, id uint16) bool {
	if id == 0 {
		return false
	}
	if p.mount != nil && p.mount.vehicle.definition.WeaponTypeID == int32(id) {
		return true
	}
	for _, equipment := range p.throwEquipment {
		if equipment.WeaponTypeID == int32(id) {
			return true
		}
	}
	return false
}
