package authority

import "errors"

// DirectDamage applies the original projectile's distance curve, instance
// modifier and (for continuous/charged shots) elapsed-time conversion. The
// match owns distance, modifier and duration; none are accepted from hit reports.
// This is direct impact damage only, before target resistance or splash rules.
func (p Projectile) DirectDamage(distance float32, elapsedMS uint32, modifier float32) (float32, error) {
	if !nonnegative(distance, modifier) {
		return 0, errors.New("invalid damage input")
	}
	curve := p.Damage
	var damage float32
	switch {
	case distance <= curve.NearDistance:
		damage = curve.NearDamage
	case distance < curve.FarDistance:
		damage = (distance-curve.NearDistance)*(curve.FarDamage-curve.NearDamage)/(curve.FarDistance-curve.NearDistance) + curve.NearDamage
	default:
		damage = curve.FarDamage
	}
	damage *= modifier
	if p.DamagePerSecond {
		damage *= float32(elapsedMS) * .001
	}
	if !nonnegative(damage) {
		return 0, errors.New("damage input overflows")
	}
	return damage, nil
}
