package authority

import "errors"

// Native add-on identities, after catalogue selection resolution. These are
// protocol identities rather than extracted balance parameters.
const (
	AddonDamage byte = 1 + iota
	AddonFireSpeed
	AddonAccuracy
	AddonReload
	AddonAmmo
	AddonSuppressor
)

type WeaponModifiers struct {
	AmmoCapacity, FireSpeed, LauncherDelay, ReloadSpeed, Spread, Damage float32
	Suppressed                                                          bool
}

func (a WeaponAddons) valid() bool {
	for _, v := range []int32{a.DamagePercent, a.FireSpeedPercent, a.ReloadSpeedPercent, a.AmmoCapacityPercent} {
		if v < 0 || v > 1000 {
			return false
		}
	}
	return a.SpreadReductionPercent >= 0 && a.SpreadReductionPercent <= 100 &&
		nonnegative(a.LauncherDelayMultiplier) && a.LauncherDelayMultiplier > 0 && a.LauncherDelayMultiplier <= 100
}

// Modifiers mirrors AddOnsModifyWeapons, including its float32 percentage
// arithmetic. Repeating an equipped add-on never stacks the effect.
func (a WeaponAddons) Modifiers(addons ...byte) (WeaponModifiers, error) {
	m := WeaponModifiers{AmmoCapacity: 1, FireSpeed: 1, LauncherDelay: 1, ReloadSpeed: 1, Spread: 1, Damage: 1}
	if !a.valid() {
		return WeaponModifiers{}, errors.New("invalid weapon addon parameters")
	}
	percent := func(v int32) float32 { return float32(float32(v) * float32(.01)) }
	for _, addon := range addons {
		switch addon {
		case 0:
		case AddonDamage:
			m.Damage = 1 + percent(a.DamagePercent)
		case AddonFireSpeed:
			m.FireSpeed = 1 + percent(a.FireSpeedPercent)
			m.LauncherDelay = a.LauncherDelayMultiplier
		case AddonAccuracy:
			m.Spread = 1 - percent(a.SpreadReductionPercent)
		case AddonReload:
			m.ReloadSpeed = 1 + percent(a.ReloadSpeedPercent)
		case AddonAmmo:
			m.AmmoCapacity = 1 + percent(a.AmmoCapacityPercent)
		case AddonSuppressor:
			m.Suppressed = true
		default:
			return WeaponModifiers{}, errors.New("unknown native weapon addon")
		}
	}
	return m, nil
}
