package match

import (
	"fmt"
	"math"
	"time"

	"nova3.test/backend/internal/authority"
	"nova3.test/backend/internal/collision"
	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/world"
)

// A bullet is created only by an admitted discharge. Position, travel distance,
// target pose and damage are all computed here, never taken from a hit claim.
type bullet struct {
	owner               *peer
	bornAt              uint32
	weapon              *weaponState
	projectile          authority.Projectile
	position, direction world.Vec3
	travelled           float64
	last                time.Time
	discharge           *dischargeHit
	sourceObject        uint32
	tail                world.Vec3
	expires             time.Time
	fireTime            uint32
	started             time.Time
	haveFireTime        bool
}

type dischargeHit struct {
	counted bool
	trace   shotTrace
}

func fixedGunnerPose(anchor *authority.GunnerAnchor, pose pc.Snapshot) (collision.Vector, collision.Vector, float32, float32) {
	yaw := math.Remainder(pc.DecodeAngle(pose.Yaw)-float64(anchor.Rotation[2]), 360)
	sine, cosine := math.Sincos(yaw * math.Pi / 180)
	dx, dy := float64(anchor.Seat[0]-anchor.Pivot[0]), float64(anchor.Seat[1]-anchor.Pivot[1])
	position := collision.Vector{anchor.Pivot[0] + float32(cosine*dx-sine*dy), anchor.Pivot[1] + float32(sine*dx+cosine*dy), anchor.Pivot[2]}
	rotation := collision.Vector(anchor.Rotation)
	rotation[2] += float32(yaw)
	pitch := float32(math.Remainder(pc.DecodeAngle(pose.Pitch)-float64(anchor.Rotation[0]), 360))
	return position, rotation, float32(yaw), pitch
}

func (b bullet) countHit() {
	if b.discharge != nil {
		if b.discharge.counted {
			return
		}
		b.discharge.counted = true
	}
	b.owner.life.hits++
}

func (s *Session) collider(p *peer, now time.Time) *collision.Actor {
	if s.authority == nil || s.authority.Collision() == nil || s.movement == nil {
		return nil
	}
	if p.collider != nil && !p.collider.SelectWeapon(int32(p.weapon), s.elapsed(now)) {
		return nil
	}
	if p.collider == nil {
		p.collider = collision.NewActor(s.authority.Collision(), int32(p.weapon), s.elapsed(now))
		if p.collider != nil {
			pose := s.movement.poses[p.player.Slot]
			p.collider.Observe(collision.Vector(pose.Position), s.elapsed(now))
			if p.mount != nil && p.mount.vehicle.definition.Category == 3 {
				p.collider.SetMachinegun(true, s.elapsed(now))
			}
		}
	}
	return p.collider
}
func (s *Session) collisionAction(p *peer, action int, now time.Time) {
	if a := s.collider(p, now); a != nil {
		a.Action(action, p.aiming, 1, s.elapsed(now))
	}
}
func (s *Session) targetRegions(now time.Time) map[*peer][]world.HitRegion {
	regions := make(map[*peer][]world.HitRegion, len(s.peers))
	for _, p := range s.peers {
		if p.departed || p.life.dead || !p.sceneReady || p.roundSpawnPending {
			continue
		}
		if p.mount != nil && p.mount.vehicle.definition.Category == 1 {
			regions[p] = p.mount.vehicle.occupantRegions(p.mount.seat)
			continue
		}
		if a := s.collider(p, now); a != nil {
			pose := s.movement.poses[p.player.Slot]
			if p.mount != nil {
				v := p.mount.vehicle
				if v.definition.Category == 3 && v.definition.Gunner != nil {
					// RemotePlayer.Update rotates the seat's horizontal offset
					// around the gun pivot, but uses the pivot's Z for the actor
					// root. The authored seat Z is only its initial placement.
					position, rotation, yaw, pitch := fixedGunnerPose(v.definition.Gunner, pose)
					regions[p] = a.MachinegunRegions(position, rotation, yaw, pitch, s.elapsed(now))
				}
				continue
			}
			regions[p] = a.Regions(collision.Vector(pose.Position), float32(pc.DecodeAngle(pose.Yaw)), float32(pc.DecodeAngle(pose.Pitch)), s.elapsed(now))
		}
	}
	return regions
}

func (s *Session) advanceProjectiles(now time.Time, out *[]Delivery) error {
	if len(s.bullets) == 0 || s.authority == nil || s.authority.Collision() == nil {
		return nil
	}
	model := s.authority.Collision()
	alive := s.bullets[:0]
	for _, b := range s.bullets {
		if b.owner.departed || b.owner.life.bornAt != b.bornAt {
			continue
		}
		regions, mountRegions := s.shotTargets(b, now)
		if b.projectile.DamagePerSecond || b.projectile.Railgun {
			keep, err := s.advanceContinuous(&b, regions, mountRegions, now, out)
			if err != nil {
				return err
			}
			if keep {
				alive = append(alive, b)
			}
			continue
		}
		remaining := float64(b.weapon.definition.Range) - b.travelled
		step := min(now.Sub(b.last).Seconds()*float64(b.projectile.Speed), remaining)
		if b.projectile.Ray {
			step = remaining
		}
		if step <= 0 {
			if now.Before(b.last) || now.Equal(b.last) {
				alive = append(alive, b)
			}
			continue
		}
		end := b.position.Add(b.direction.Mul(step))
		var ignore *uint32
		if b.sourceObject != 0 {
			ignore = &b.sourceObject
		}
		wall, blocked, err := s.movement.scene.Raycast(b.position, end, world.Filter{Group: 0x401, Mask: 0x76f7}, ignore)
		if err != nil {
			return err
		}
		if blocked {
			end = wall.Point
		}
		b.recordTrace(b.position, end, wall.Object, regions, mountRegions)
		shot := world.Capsule{A: b.position, B: end, Radius: float64(model.ShotRadius)}
		var target *peer
		var vehicle *mountState
		var impact world.RegionContact
		nearest := math.Inf(1)
		for _, p := range s.peers {
			if p == b.owner || p.life.dead {
				continue
			}
			contact, hit, err := world.ContactRegions(shot, regions[p])
			if err != nil {
				return fmt.Errorf("projectile player target slot=%d: %w", p.player.Slot, err)
			}
			if hit {
				delta := contact.Point.Sub(b.position)
				distance := delta.Dot(delta)
				if distance < nearest {
					nearest, target, impact = distance, p, contact
				}
			}
		}
		for _, v := range s.mounts {
			if b.sourceObject != 0 && b.sourceObject == v.definition.ObjectID {
				continue
			}
			contact, hit, err := world.ContactRegions(shot, mountRegions[v])
			if err != nil {
				return fmt.Errorf("projectile mount target category=%d index=%d: %w", v.definition.Category, v.definition.WireIndex, err)
			}
			if hit {
				delta := contact.Point.Sub(b.position)
				if distance := delta.Dot(delta); distance < nearest {
					nearest, target, vehicle, impact = distance, nil, v, contact
				}
			}
		}
		if vehicle != nil {
			if b.projectile.ExplosionDamage > 0 {
				if err := s.explode(b, impact.Point, regions, now, out); err != nil {
					return err
				}
			} else {
				distance := float32(b.travelled + math.Sqrt(nearest))
				amount, err := b.projectile.DirectDamage(distance, 0, b.weapon.definition.BaseDamageMultiplier*b.weapon.modifiers.Damage*impact.DamageMultiplier*b.projectile.VehicleDamagePercent*.01)
				if err != nil {
					return err
				}
				before := vehicle.health
				if err := s.damageVehicle(b.owner, vehicle, uint16(b.weapon.definition.TypeID), amount, now, out); err != nil {
					return err
				}
				if vehicle.health < before {
					b.countHit()
				}
			}
			continue
		}
		if target != nil {
			if b.projectile.ExplosionDamage > 0 {
				if err := s.explode(b, impact.Point, regions, now, out); err != nil {
					return err
				}
				continue
			}
			if !s.canDamage(b.owner, target) {
				continue
			}
			distance := float32(b.travelled + math.Sqrt(nearest))
			amount, err := b.projectile.DirectDamage(distance, 0, b.weapon.definition.BaseDamageMultiplier*b.weapon.modifiers.Damage*impact.DamageMultiplier)
			if err != nil {
				return err
			}
			if s.loading.Mode == Instagib || s.loading.Mode == TeamInstagib {
				amount = target.life.health
			}
			b.countHit()
			amount, err = s.consumableImpact(b.owner, target, amount, now)
			if err != nil {
				return err
			}
			if err = s.damageImpact(b.owner, target, uint16(b.weapon.definition.TypeID), amount, byte(impact.PartID), now, out); err != nil {
				return err
			}
			continue
		}
		if blocked {
			if err := s.explode(b, wall.Point, regions, now, out); err != nil {
				return err
			}
			continue
		}
		b.travelled += step
		b.position = end
		b.last = now
		if b.travelled < float64(b.weapon.definition.Range) {
			alive = append(alive, b)
		}
	}
	s.bullets = alive
	return nil
}

// Original multiplayer event41 selects the nearest animated body-part center.
// Its strict range gate, surface-distance falloff and inside-part multiplier
// differ from direct bullet damage. The radial event does not raycast walls;
// the moving projectile's collision decides where the explosion originates.
func (s *Session) explode(b bullet, center world.Vec3, regions map[*peer][]world.HitRegion, now time.Time, out *[]Delivery) error {
	if b.projectile.ExplosionDamage <= 0 || b.projectile.ExplosionRadius <= 0 {
		return nil
	}
	radius := float64(b.projectile.ExplosionRadius)
	hit := false
	defer func() {
		if hit {
			b.countHit()
		}
	}()
	for _, p := range s.peers {
		if p.life.dead {
			continue
		}
		nearest := math.Inf(1)
		var part world.HitRegion
		for _, r := range regions[p] {
			d := r.Volume.A.Add(r.Volume.B).Mul(.5).Sub(center)
			if squared := d.Dot(d); squared < nearest {
				nearest, part = squared, r
			}
		}
		if nearest >= radius*radius {
			continue
		}
		// Native computeClosestBodyPart stores getRadius() in output+8;
		// event41 takes its square root in the surface-distance expression.
		factor := 1 - max(math.Sqrt(nearest)-math.Sqrt(part.Volume.Radius), 0)/radius
		amount := float32(factor) * b.projectile.ExplosionDamage
		if nearest <= part.Volume.Radius {
			amount *= part.DamageMultiplier
		}
		before := p.life.health
		if err := s.damageImpact(b.owner, p, uint16(b.weapon.definition.TypeID), amount, 0, now, out); err != nil {
			return err
		}
		if p != b.owner && p.life.health < before {
			hit = true
		}
	}
	for _, v := range s.mounts {
		nearest := math.Inf(1)
		var part world.HitRegion
		for _, candidate := range v.regions() {
			delta := candidate.Volume.A.Add(candidate.Volume.B).Mul(.5).Sub(center)
			if distance := delta.Dot(delta); distance < nearest {
				nearest, part = distance, candidate
			}
		}
		if nearest >= radius*radius {
			continue
		}
		factor := 1 - max(math.Sqrt(nearest)-math.Sqrt(part.Volume.Radius), 0)/radius
		amount := float32(factor) * b.projectile.ExplosionDamage
		if nearest <= part.Volume.Radius {
			amount *= part.DamageMultiplier
		}
		before := v.health
		if err := s.damageVehicle(b.owner, v, uint16(b.weapon.definition.TypeID), amount, now, out); err != nil {
			return err
		}
		hit = hit || v.health < before
	}
	return nil
}
