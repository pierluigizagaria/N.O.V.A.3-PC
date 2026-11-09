package match

import (
	"encoding/binary"
	"math"
	"nova3.test/backend/internal/authority"
	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/world"
	"time"
)

type thrownGrenade struct {
	owner              *peer
	definition         authority.Grenade
	weapon             uint16
	position, velocity world.Vec3
	last, expires      time.Time
	resting            bool
}

func (s *Session) throwGrenade(p *peer, message []byte, now time.Time, out *[]Delivery) error {
	p.throwIntents++
	p.throwReject = 1
	if s.authority == nil || s.movement == nil || s.movement.scene == nil || p.life.dead || p.life.frozen || p.roundSpawnPending || s.round.runningSince.IsZero() || len(s.grenades) >= 256 {
		return nil
	}
	v, _ := game.ReadGrenadeThrow(message)
	g, ok := s.authority.WireGrenade(v.Grenade)
	p.throwReject = 2
	if !ok {
		return nil
	}
	age := int32(s.elapsed(now) - v.Time)
	p.throwReject = 3
	if age < -2000 || age > 2000 || int32(v.Time-p.life.bornAt) < 0 || (p.haveThrow && int32(v.Time-p.lastThrow) <= 0) {
		return nil
	}
	weapon, knownWeapon := s.authority.WireWeapon(v.Weapon)
	p.throwReject = 4
	launcher := knownWeapon && s.equippedWeapon(p, uint16(weapon.TypeID)) && weapon.FireModes[0].ProjectileTypeID == g.TypeID
	// Native hand throws encode -1 (byte255), and event41 then uses the
	// grenade's type for the death notification. Launcher throws name a weapon.
	equipment, ownsEquipment := p.throwEquipment[g.TypeID]
	if !launcher && (!ownsEquipment || v.Weapon != 255 && v.Weapon != equipment.WeaponWireIndex || p.throwStock[g.TypeID] <= 0) {
		return nil
	}
	killWeapon := uint16(g.TypeID)
	if !launcher && equipment.WeaponTypeID > 0 {
		killWeapon = uint16(equipment.WeaponTypeID)
	}
	if launcher {
		killWeapon = uint16(weapon.TypeID)
	}
	origin, err := s.movement.bounds.DecodePosition(v.Origin)
	p.throwReject = 5
	if err != nil {
		return nil
	}
	direction, err := world.DecodeShotDirection(v.Direction)
	p.throwReject = 6
	if err != nil {
		return nil
	}
	// Movement is a signed native vector (zero is valid), not a hit claim.
	movement := world.Vec3{X: float64(v.Movement[0])/32767.5 - 1, Y: float64(v.Movement[1])/32767.5 - 1, Z: float64(v.Movement[2])/32767.5 - 1}
	if movement.Dot(movement) > 1.02 {
		return nil
	}
	pose := s.movement.poses[p.player.Slot].Position
	body := s.authority.Player().Body
	center := world.Vec3{X: float64(pose[0] + body.OffsetX), Y: float64(pose[1] + body.OffsetY), Z: float64(pose[2] + body.OffsetZ + body.Height*.5)}
	cell := s.movement.bounds.Maximum.Sub(s.movement.bounds.Minimum).Mul(1. / 65535)
	limit := float64(body.Height*.5+body.Radius) + math.Sqrt(cell.Dot(cell))
	offset := origin.Sub(center)
	p.throwReject = 7
	if offset.Dot(offset) > limit*limit {
		return nil
	}
	if _, blocked, err := s.movement.scene.Raycast(center, origin, world.Filter{Group: 0x401, Mask: 0x76f7}, nil); blocked || err != nil {
		return err
	}
	speed := float64(g.Speed) * (1 + max(direction.Dot(movement), 0))
	p.throwReject = 8
	if v.Speed > 0 {
		if float64(v.Speed) > float64(g.Speed)*2 {
			return nil
		}
		speed = float64(v.Speed)
	}
	if speed <= 0 {
		return nil
	}
	// The launcher spends its own magazine. Hand throws spend the selected
	// server-owned equipment stock, reset only by a granted new life.
	if launcher {
		w := p.weapons[uint16(weapon.TypeID)]
		if w == nil {
			return nil
		}
		interval, err := weapon.FireInterval(0, w.modifiers)
		if err != nil || !w.fire.Allows(now, interval) || !s.burstAllows(w, 0, now) || !w.ammo.Consume(now) {
			return nil
		}
		w.fire.Commit(now, interval)
		s.commitBurst(w, 0, now)
	} else {
		if p.throwStock[g.TypeID] <= 0 {
			return nil
		}
		p.throwStock[g.TypeID]--
	}
	p.throwReject = 0
	p.throwAccepted++
	p.lastThrow, p.haveThrow = v.Time, true
	p.life.shots++
	born := now.Add(-time.Duration(max(age, 0)) * time.Millisecond)
	s.grenades = append(s.grenades, thrownGrenade{owner: p, definition: g, weapon: killWeapon, position: origin, velocity: direction.Mul(speed), last: born, expires: born.Add(time.Duration(g.TimeToLiveMS) * time.Millisecond)})
	return s.present(p, message, true, now, out)
}

func (s *Session) detonateMines(p *peer, now time.Time, out *[]Delivery) error {
	found := false
	for i := range s.grenades {
		g := &s.grenades[i]
		if g.owner == p && g.definition.Sticky {
			g.expires = now
			found = true
		}
	}
	if found {
		return s.present(p, []byte{game.DetonateMine, p.player.Slot}, true, now, out)
	}
	return nil
}

func (s *Session) grenadeExplosion(g thrownGrenade, now time.Time, out *[]Delivery) error {
	regions := s.targetRegions(now)
	if g.definition.Effect != 0 {
		for target, parts := range regions {
			nearest := math.Inf(1)
			var part world.HitRegion
			for _, r := range parts {
				d := r.Volume.A.Add(r.Volume.B).Mul(.5).Sub(g.position)
				if distance := d.Dot(d); distance < nearest {
					nearest, part = distance, r
				}
			}
			radius := float64(g.definition.DamageRadius)
			if nearest >= radius*radius {
				continue
			}
			message := []byte{53, target.player.Slot, 0, 0, 0, 0}
			duration := g.definition.EffectDurationMS
			if g.definition.Effect == 1 {
				factor := 1 - max(math.Sqrt(nearest)-math.Sqrt(part.Volume.Radius), 0)/radius
				duration = int32(float64(duration) * factor * float64(part.DamageMultiplier))
			} else {
				if now.Before(target.chemicalUntil) {
					continue
				}
				target.chemicalUntil = now.Add(time.Duration(duration) * time.Millisecond)
				message[0] = 148
				message = append(message, byte(g.definition.EffectStrength))
			}
			binary.BigEndian.PutUint32(message[2:], uint32(max(duration, 0)))
			if err := s.emit(target, message, true, now, out); err != nil {
				return err
			}
		}
	}
	b := bullet{owner: g.owner, weapon: &weaponState{definition: authority.Weapon{TypeID: int32(g.weapon)}}, projectile: authority.Projectile{ExplosionDamage: g.definition.Damage, ExplosionRadius: g.definition.DamageRadius}}
	return s.explode(b, g.position, regions, now, out)
}

func (s *Session) advanceGrenades(now time.Time, out *[]Delivery) error {
	if len(s.grenades) == 0 {
		return nil
	}
	physics := s.authority.GrenadePhysics()
	regions := s.targetRegions(now)
	alive := s.grenades[:0]
	for _, g := range s.grenades {
		if g.owner.departed {
			continue
		}
		until := minTime(now, g.expires)
		for g.last.Before(until) {
			step := min(until.Sub(g.last), time.Second/120)
			dt := step.Seconds()
			// The native grenade tests animated target regions before the map.
			// A resting mine retains its contact trigger; its owner is excluded.
			radius := max(float64(physics.BodyRadius), float64(g.definition.ContactRadius))
			path := world.Capsule{A: g.position, B: g.position.Add(g.velocity.Mul(dt)), Radius: radius}
			contact := false
			for target, parts := range regions {
				if target == g.owner {
					continue
				}
				impact, hit, err := world.ContactRegions(path, parts)
				if err != nil {
					return err
				}
				if hit {
					g.position = impact.Point
					contact = true
					break
				}
			}
			// Native grenade target queries include static targetable objects,
			// including vehicle bodies, before checking map collision.
			if !contact {
				for _, vehicle := range s.mounts {
					impact, hit, err := world.ContactRegions(path, vehicle.regions())
					if err != nil {
						return err
					}
					if hit {
						g.position = impact.Point
						contact = true
						break
					}
				}
			}
			if contact {
				g.expires = now
				break
			}
			if !g.resting {
				delta := g.velocity.Mul(dt)
				radius := float64(physics.BodyRadius)
				hit, blocked, err := s.movement.scene.CapsuleCast(world.Capsule{A: g.position, B: g.position, Radius: radius}, delta, world.Filter{Group: 0x401, Mask: 0x76f7}, nil)
				if err != nil {
					return err
				}
				if blocked {
					g.position = g.position.Add(delta.Mul(hit.Fraction)).Add(hit.Normal.Mul(float64(physics.SurfaceClearance)))
					if g.definition.Sticky {
						g.resting = true
						g.velocity = world.Vec3{}
					} else {
						g.velocity = g.velocity.Sub(hit.Normal.Mul(2 * g.velocity.Dot(hit.Normal))).Mul(float64(physics.BounceRetention))
						g.resting = g.velocity.Dot(g.velocity) <= float64(physics.StopSpeedSquared)
					}
				} else {
					g.position = g.position.Add(delta)
				}
				if !g.resting {
					drag := float64(g.definition.Drag) * dt
					horizontal := math.Hypot(g.velocity.X, g.velocity.Y)
					if horizontal > drag && horizontal > 0 {
						g.velocity.X *= 1 - drag/horizontal
						g.velocity.Y *= 1 - drag/horizontal
					}
					if drag < g.velocity.Z {
						g.velocity.Z -= drag
					}
					g.velocity.Z += float64(physics.Gravity) * dt
				}
			}
			g.last = g.last.Add(step)
		}
		if !now.Before(g.expires) {
			if err := s.grenadeExplosion(g, now, out); err != nil {
				return err
			}
		} else {
			alive = append(alive, g)
		}
	}
	s.grenades = alive
	return nil
}

func minTime(a, b time.Time) time.Time {
	if a.Before(b) {
		return a
	}
	return b
}
