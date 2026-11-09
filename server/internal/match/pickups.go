package match

import (
	"math/rand/v2"
	"time"

	"nova3.test/backend/internal/authority"
	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/world"
)

type pickupState struct {
	object           int32
	zone             int32
	position         world.Vec3
	choices          []authority.PickupChoice
	linked           byte
	visible          bool
	dropped          bool
	owner            byte
	readyAt, expires time.Time
}

func (s *Session) initPickups() error {
	if s.authority == nil {
		return nil
	}
	for _, p := range s.authority.Pickups(s.loading.Map) {
		if p.Modes >= 0 && p.Modes&modeMapBit(s.loading.Mode) == 0 {
			continue
		}
		if !s.movement.zones[p.Zone] {
			return ErrState
		}
		if _, err := s.movement.bounds.EncodePosition(world.Vec3{X: float64(p.Position[0]), Y: float64(p.Position[1]), Z: float64(p.Position[2])}); err != nil {
			return err
		}
		s.pickups = append(s.pickups, &pickupState{object: p.ObjectID, zone: p.Zone,
			position: world.Vec3{X: float64(p.Position[0]), Y: float64(p.Position[1]), Z: float64(p.Position[2])},
			choices:  p.Choices, linked: p.InitialChoice, visible: true})
	}
	s.nextPickupID = s.authority.PickupRules().FirstDynamicObjectID
	return nil
}

func (s *Session) pickupMessage(p *pickupState) ([]byte, error) {
	c := p.choices[p.linked]
	position, err := s.movement.bounds.EncodePosition(p.position)
	if err != nil {
		return nil, err
	}
	v := game.PowerupState{Object: p.object, Weapon: uint16(c.WeaponTypeID), Power: uint16(c.PowerTypeID),
		Ammo: byte(c.Ammo), Position: position, Zone: p.zone, Linked: p.linked}
	if p.visible {
		v.Flags = 1
	}
	if p.dropped {
		v.Flags |= 2
		v.State = p.owner
	}
	return v.Message()
}

func (s *Session) pickupScene(p *peer, now time.Time, out *[]Delivery) error {
	for _, item := range s.pickups {
		m, err := s.pickupVisibility(item)
		if err != nil {
			return err
		}
		if err = s.emit(p, m, true, now, out); err != nil {
			return err
		}
	}
	return nil
}

func (s *Session) pickupVisibility(item *pickupState) ([]byte, error) {
	var body []byte
	if !item.dropped {
		// Native39 toggles only the named child; it does not stop siblings.
		// Send their actual disabled state together before the selected child,
		// including for a late join whose authored first child starts enabled.
		for i := range item.choices {
			if i == int(item.linked) {
				continue
			}
			hidden := *item
			hidden.linked = byte(i)
			hidden.visible = false
			m, err := s.pickupMessage(&hidden)
			if err != nil {
				return nil, err
			}
			body = append(body, m...)
		}
	}
	m, err := s.pickupMessage(item)
	return append(body, m...), err
}

func (s *Session) grantPickup(p *peer, c authority.PickupChoice, now time.Time) (bool, error) {
	id := uint16(c.WeaponTypeID)
	if w := p.weapons[id]; w != nil {
		return w.ammo.GiveTotal(c.Ammo, now) > 0, nil
	}
	if c.AmmoOnly {
		return false, nil
	}
	definition, ok := s.authority.Weapon(c.WeaponTypeID)
	if !ok {
		// One authored legacy child has no multiplayer collision action table.
		// Keep its authored identity visible; do not grant an unhittable actor.
		return false, nil
	}
	modifiers, err := s.authority.WeaponAddons().Modifiers()
	if err != nil {
		return false, err
	}
	ammo, err := authority.NewEquippedAmmo(definition, modifiers)
	if err != nil {
		return false, err
	}
	// Native acquisition of a previously unavailable weapon grants its max
	// ammunition. Subsequent pickups add only the prefab's authored quantity.
	p.weapons[id] = &weaponState{definition: definition, modifiers: modifiers, ammo: ammo}
	return true, nil
}

func (s *Session) advancePickups(now time.Time, out *[]Delivery) error {
	if s.authority == nil || s.movement == nil {
		return nil
	}
	kept := s.pickups[:0]
	for _, item := range s.pickups {
		if item.dropped && !now.Before(item.expires) {
			item.visible = false
			m, err := s.pickupVisibility(item)
			if err != nil {
				return err
			}
			if err = s.announce(m, now, out); err != nil {
				return err
			}
			continue
		}
		kept = append(kept, item)
		if !item.visible && !item.readyAt.IsZero() && now.After(item.readyAt) {
			item.linked = byte(rand.IntN(len(item.choices)))
			item.visible = true
			item.readyAt = time.Time{}
			m, err := s.pickupVisibility(item)
			if err != nil {
				return err
			}
			if err = s.announce(m, now, out); err != nil {
				return err
			}
		}
		if !item.visible || s.round.runningSince.IsZero() {
			continue
		}
		c := item.choices[item.linked]
		for _, p := range s.peers {
			if !p.sceneReady || p.departed || p.life.dead || p.life.frozen || p.roundSpawnPending || p.mount != nil {
				continue
			}
			pose := s.movement.poses[p.player.Slot].Position
			d := world.Vec3{X: float64(pose[0]), Y: float64(pose[1]), Z: float64(pose[2])}.Sub(item.position)
			radius := float64(c.Radius + s.authority.Player().Body.Radius)
			if d.Dot(d) >= radius*radius {
				continue
			}
			granted, err := s.grantPickup(p, c, now)
			if err != nil {
				return err
			}
			if !granted {
				continue
			}
			item.visible = false
			if !item.dropped && c.RespawnMS > 0 {
				item.readyAt = now.Add(time.Duration(c.RespawnMS) * time.Millisecond)
			}
			m, err := (game.PowerupConsumption{Actor: p.player.Slot, Object: item.object, Linked: item.linked}).Message()
			if err != nil {
				return err
			}
			if err = s.announce(m, now, out); err != nil {
				return err
			}
			if item.dropped {
				kept = kept[:len(kept)-1]
			}
			break
		}
	}
	s.pickups = kept
	return nil
}

func (s *Session) dropWeapon(p *peer, now time.Time, out *[]Delivery) error {
	if s.authority == nil || s.movement == nil {
		return nil
	}
	if s.loading.Mode == Instagib || s.loading.Mode == TeamInstagib {
		return nil
	}
	c, ok := s.authority.WeaponDrop(int32(p.weapon))
	if !ok {
		return nil
	}
	r := s.authority.PickupRules()
	if s.nextPickupID == 0 || s.nextPickupID >= r.DynamicObjectLimit {
		return nil
	}
	pose := s.movement.poses[p.player.Slot].Position
	position := world.Vec3{X: float64(pose[0]), Y: float64(pose[1]), Z: float64(pose[2] + r.DropGroundOffset)}
	hit, found, err := s.movement.scene.Raycast(position, position.Add(world.Vec3{Z: -float64(r.DropGroundDistance)}), world.Filter{Group: 1, Mask: 0x7ef7}, nil)
	if err != nil {
		return err
	}
	if found {
		position = hit.Point
	}
	if _, err = s.movement.bounds.EncodePosition(position); err != nil {
		return nil
	}
	// A player can die before crossing any zone boundary. Native CGameObject
	// already has the spawn's zone even though it has sent no107 transition.
	zone, knownZone := s.authority.SpawnZone(s.loading.Map, s.movement.spawns[p.player.Slot].ID)
	if p.zone != nil {
		zone, knownZone = p.zone.Zone, true
	}
	if !knownZone || !s.movement.zones[zone] {
		return nil
	}
	item := &pickupState{object: s.nextPickupID, zone: zone, position: position, choices: []authority.PickupChoice{c}, visible: true, dropped: true, owner: p.player.Slot, expires: now.Add(time.Duration(r.DropLifetimeMS) * time.Millisecond)}
	s.nextPickupID++
	s.pickups = append(s.pickups, item)
	m, err := s.pickupMessage(item)
	if err != nil {
		return err
	}
	return s.announce(m, now, out)
}

func (s *Session) resetPickups(now time.Time, out *[]Delivery) error {
	kept := s.pickups[:0]
	for _, p := range s.pickups {
		p.visible = !p.dropped
		p.readyAt = time.Time{}
		if !p.dropped {
			kept = append(kept, p)
		}
		m, err := s.pickupVisibility(p)
		if err != nil {
			return err
		}
		if err = s.announce(m, now, out); err != nil {
			return err
		}
	}
	s.pickups = kept
	return nil
}
