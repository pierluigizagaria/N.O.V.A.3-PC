package match

import (
	"math"
	"time"

	"nova3.test/backend/internal/world"
)

// Wi-Fi railgun/flame damage is duration-based. The beam keeps its launch
// direction; fade-out is presentation and cannot extend its damage lifetime.
func (s *Session) advanceContinuous(b *bullet, regions map[*peer][]world.HitRegion, mountRegions map[*mountState][]world.HitRegion, now time.Time, out *[]Delivery) (bool, error) {
	until := now
	if until.After(b.expires) {
		until = b.expires
	}
	elapsed := until.Sub(b.last)
	if elapsed <= 0 {
		return now.Before(b.expires), nil
	}
	from, end := b.position, b.position
	step := float64(0)
	if b.projectile.Railgun {
		end = end.Add(b.direction.Mul(min(float64(b.projectile.RayLength), float64(b.weapon.definition.Range))))
	} else {
		step = min(elapsed.Seconds()*float64(b.projectile.Speed), float64(b.weapon.definition.Range)-b.travelled)
		if step <= 0 {
			return false, nil
		}
		from = b.tail
		end = end.Add(b.direction.Mul(step))
	}
	var ignore *uint32
	if b.sourceObject != 0 {
		ignore = &b.sourceObject
	}
	var wall world.Hit
	var blocked bool
	var err error
	radius := float64(b.projectile.ContactRadius)
	if b.projectile.Railgun {
		// Native railgun launch/update clips against groups2/4, not the
		// travelling bullet's broader map/prop filter.
		wall, blocked, err = s.movement.scene.Raycast(from, end, world.Filter{Group: 0x7ef7, Mask: 6}, ignore)
	} else if radius > 0 {
		wall, blocked, err = s.movement.scene.CapsuleCast(world.Capsule{A: from, B: from, Radius: radius}, end.Sub(from), world.Filter{Group: 0x401, Mask: 0x76f7}, ignore)
	} else {
		wall, blocked, err = s.movement.scene.Raycast(from, end, world.Filter{Group: 0x401, Mask: 0x76f7}, ignore)
	}
	if err != nil {
		return false, err
	}
	if blocked {
		end = wall.Point
	}
	b.recordTrace(from, end, wall.Object, regions, mountRegions)
	shot := world.Capsule{A: from, B: end, Radius: max(radius, float64(s.authority.Collision().ShotRadius))}
	modifier := b.weapon.definition.BaseDamageMultiplier * b.weapon.modifiers.Damage
	amountAt := func(contact world.RegionContact, vehicle bool) (float32, error) {
		delta := contact.Point.Sub(b.position)
		distance := float32(b.travelled + math.Sqrt(delta.Dot(delta)))
		factor := modifier * contact.DamageMultiplier
		if vehicle {
			factor *= b.projectile.VehicleDamagePercent * .01
		}
		perSecond, err := b.projectile.DirectDamage(distance, 1000, factor)
		return perSecond * float32(elapsed.Seconds()), err
	}
	// Native multiplayer target events visit each body once per update. This
	// preserves piercing humans while the map ray still bounds the whole beam.
	for _, p := range s.peers {
		if p == b.owner || p.life.dead || !s.canDamage(b.owner, p) {
			continue
		}
		contact, hit, err := world.ContactRegions(shot, regions[p])
		if err != nil {
			return false, err
		}
		if !hit {
			continue
		}
		amount, err := amountAt(contact, false)
		if err != nil {
			return false, err
		}
		// MP_BulletHit applies the mode override to every human projectile
		// contact, including the duration-based railgun path.
		if s.loading.Mode == Instagib || s.loading.Mode == TeamInstagib {
			amount = p.life.health
		}
		amount, err = s.consumableImpact(b.owner, p, amount, now)
		if err != nil {
			return false, err
		}
		before := p.life.health
		// Keep the native1/32-unit channel for fractional DPS. Sending a large
		// integer-only packet each30Hz tick would discard much of the beam.
		for amount >= 1./32 && !p.life.dead {
			part := min(amount, float32(255./32))
			if err = s.damageImpact(b.owner, p, uint16(b.weapon.definition.TypeID), part, byte(contact.PartID), now, out); err != nil {
				return false, err
			}
			amount -= part
		}
		if p.life.health < before {
			b.countHit()
		}
	}
	for _, v := range s.mounts {
		if b.sourceObject != 0 && b.sourceObject == v.definition.ObjectID {
			continue
		}
		contact, hit, err := world.ContactRegions(shot, mountRegions[v])
		if err != nil {
			return false, err
		}
		if !hit {
			continue
		}
		amount, err := amountAt(contact, true)
		if err != nil {
			return false, err
		}
		before := v.health
		if err = s.damageVehicle(b.owner, v, uint16(b.weapon.definition.TypeID), amount, now, out); err != nil {
			return false, err
		}
		if v.health < before {
			b.countHit()
		}
	}
	b.last = until
	if !b.projectile.Railgun {
		b.tail, b.position = b.position, end
		b.travelled += step
		if blocked || b.travelled >= float64(b.weapon.definition.Range) {
			return false, nil
		}
	}
	return now.Before(b.expires), nil
}
