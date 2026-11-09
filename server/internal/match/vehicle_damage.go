package match

import (
	"math"
	"nova3.test/backend/internal/collision"
	"time"

	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/world"
)

func (v *mountState) regions() []world.HitRegion {
	if v.health <= 0 {
		return nil
	}
	if v.mech != nil {
		return v.mech.Regions()
	}
	x, y, z := v.rotation[0]*math.Pi/180, v.rotation[1]*math.Pi/180, v.rotation[2]*math.Pi/180
	sx, cx := math.Sincos(x)
	sy, cy := math.Sincos(y)
	sz, cz := math.Sincos(z)
	place := func(p [3]float32) world.Vec3 {
		a, b, c := float64(p[0]), float64(p[1]), float64(p[2])
		return v.position.Add(world.Vec3{X: cz*cy*a + (sy*sx*cz-cx*sz)*b + (sy*cx*cz+sz*sx)*c, Y: sz*cy*a + (sy*sx*sz+cx*cz)*b + (sy*cx*sz-cz*sx)*c, Z: -sy*a + cy*sx*b + cy*cx*c})
	}
	parts := make([]world.HitRegion, 0, len(v.definition.Capsules))
	for i, part := range v.definition.Capsules {
		parts = append(parts, world.HitRegion{PartID: uint16(i + 1), Volume: world.Capsule{A: place(part.A), B: place(part.B), Radius: float64(part.Radius)}, DamageMultiplier: part.DamageMultiplier})
	}
	return parts
}

func (s *Session) resetMech(v *mountState, now time.Time) {
	if v.definition.Category == 2 {
		if model := s.authority.MechCollision(); model != nil && model.TypeID == v.definition.TypeID {
			v.mech = collision.NewMech(model, collision.Vector{float32(v.position.X), float32(v.position.Y), float32(v.position.Z)}, float32(v.rotation[2]), s.elapsed(now))
		}
	}
}

func (s *Session) damageVehicle(attacker *peer, v *mountState, weapon uint16, amount float32, now time.Time, out *[]Delivery) error {
	if v.health <= 0 || amount <= 0 || math.IsNaN(float64(amount)) || math.IsInf(float64(amount), 0) || s.round.runningSince.IsZero() {
		return nil
	}
	amount = min(amount, v.health)
	v.health -= amount
	// Native23 gives clients the hit/hurt effect; native30 owns the static
	// object's death. Neither is accepted as a client damage report.
	left := amount + v.hitRemainder
	for left >= 1 {
		part := byte(min(left, 255))
		message, _ := (game.Hit{Object: true, Attacker: attacker.player.Slot, Victim: v.definition.StaticWireIndex, Amount: part}).Message()
		if err := s.announce(message, now, out); err != nil {
			return err
		}
		left -= float32(part)
	}
	v.hitRemainder = left
	if v.health > 0 {
		return nil
	}
	v.health = 0
	v.respawnAt = now.Add(time.Duration(v.definition.RespawnMS) * time.Millisecond)
	for _, occupant := range v.occupants {
		if occupant != nil {
			// Original destruction exits the seat and kills its occupant with
			// damage kind10. Existing player death owns score and respawn.
			if err := s.damageKind(attacker, occupant, weapon, occupant.life.health, 0, 10, 0, now, out); err != nil {
				return err
			}
		}
	}
	return s.announce([]byte{30, v.definition.StaticWireIndex}, now, out)
}

func (s *Session) advanceMounts(now time.Time, out *[]Delivery) error {
	regions := s.targetRegions(now)
	for _, v := range s.mounts {
		if v.mech != nil {
			v.mech.Advance(s.elapsed(now))
		}
		if (v.definition.Category == 1 || v.mech != nil) && v.health > 0 && v.occupants[0] != nil && !s.round.runningSince.IsZero() {
			current := make(map[byte]uint32)
			speed := math.Sqrt(v.velocity.Dot(v.velocity)) * 3.6
			if now.Sub(v.lastPose) <= 200*time.Millisecond && (v.definition.MaximumSpeedKMH > 0 || v.mech != nil) && speed > 0 {
				for victim, parts := range regions {
					if victim == v.occupants[0] || victim.mount != nil && victim.mount.vehicle == v {
						continue
					}
					for _, body := range v.regions() {
						_, hit, err := world.ContactRegions(body.Volume, parts)
						if err != nil {
							return err
						}
						if !hit {
							continue
						}
						current[victim.player.Slot] = victim.life.bornAt
						if previous, seen := v.contacts[victim.player.Slot]; seen && previous == victim.life.bornAt {
							break
						}
						rules := s.authority.VehicleImpact()
						var amount float32
						if v.mech != nil {
							amount = s.authority.MechCollision().Tuning.ContactDamage
						} else {
							fraction := float32(min(speed/float64(v.definition.MaximumSpeedKMH), 1))
							amount = s.authority.Player().BaseHealth * (rules.MinimumHealthFraction + (rules.MaximumHealthFraction-rules.MinimumHealthFraction)*fraction) * rules.DamageMultiplier
						}
						if err := s.damageKind(v.occupants[0], victim, uint16(v.definition.TypeID), amount, 0, 10, 0, now, out); err != nil {
							return err
						}
						break
					}
				}
			}
			v.contacts = current
		}
		if v.respawnAt.IsZero() || now.Before(v.respawnAt) {
			continue
		}
		v.position = world.Vec3{X: float64(v.definition.Position[0]), Y: float64(v.definition.Position[1]), Z: float64(v.definition.Position[2])}
		for axis := range v.rotation {
			v.rotation[axis] = float64(v.definition.Rotation[axis])
		}
		v.health = v.definition.Health
		v.generation++
		v.hitRemainder = 0
		v.respawnAt = time.Time{}
		v.velocity, v.lastPose, v.contacts = world.Vec3{}, time.Time{}, nil
		v.state, v.kinematics, v.shooter = nil, nil, nil
		v.weapons, v.shots = [2]*weaponState{}, [2]int32{}
		s.resetMech(v, now)
		kind := byte(136)
		if v.definition.Category == 2 {
			kind = 145
		}
		if err := s.announce([]byte{kind, v.definition.WireIndex}, now, out); err != nil {
			return err
		}
	}
	return nil
}
