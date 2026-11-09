package authority

import "errors"

// Native DoFire counts discharges per mode and starts a separate, unmodified
// cooldown after the authored burst quota. It is not magazine replenishment.
type Burst struct {
	WeaponTypeID int32 `json:"weapon_type_id"`
	Mode         byte  `json:"mode"`
	Shots        int32 `json:"shots"`
	PauseMS      int32 `json:"pause_ms"`
}

func (d *Definitions) Burst(weapon int32, mode int) (Burst, bool) {
	b, ok := d.bursts[[2]int32{weapon, int32(mode)}]
	return b, ok
}

func (d *Definitions) loadBursts(source []Burst) error {
	d.bursts = make(map[[2]int32]Burst)
	for _, b := range source {
		w, ok := d.Weapon(b.WeaponTypeID)
		if !ok {
			w, ok = d.MountedWeapon(b.WeaponTypeID)
		}
		key := [2]int32{b.WeaponTypeID, int32(b.Mode)}
		if _, duplicate := d.bursts[key]; duplicate || !ok || int(b.Mode) >= len(w.FireModes) || b.Shots <= 0 || b.PauseMS <= 0 {
			return errors.New("invalid burst fire rule")
		}
		d.bursts[key] = b
	}
	return nil
}
