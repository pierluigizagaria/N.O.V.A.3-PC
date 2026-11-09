package authority

import (
	"errors"
	"math"
	"time"
)

var ErrAmmoRules = errors.New("invalid ammunition rules")

// Ammo owns one weapon fire-mode's ammunition and base reload schedule. It is
// a simulation component, not permission to fire: cadence, loadout modifiers,
// spatial validation and damage must be checked by the match owner separately.
type Ammo struct {
	reserve, magazine           int32
	reserveLimit, magazineLimit int32
	refillDelay, finishDelay    time.Duration
	refillAt, finishAt          time.Time
	refilled, reloading         bool
}

type AmmoSnapshot struct {
	Reserve, Magazine int32
	Reloading         bool
}

func NewAmmo(weapon Weapon) (*Ammo, error) {
	return NewEquippedAmmo(weapon, WeaponModifiers{AmmoCapacity: 1, ReloadSpeed: 1})
}

func (r Reload) schedule(speed float32) (time.Duration, time.Duration, error) {
	combined := float32(r.AnimationSpeed * speed)
	if r.EventMS < 0 || r.AnimationMS <= 0 || r.EventMS > r.AnimationMS || !nonnegative(speed, r.AnimationSpeed, combined) || combined <= 0 {
		return 0, 0, ErrAmmoRules
	}
	// Native game tracks compare integer animation time to authored event
	// boundaries. The animation speed is authored speed * equipment factor.
	refill := math.Ceil(float64(float32(float32(r.EventMS) / combined)))
	finish := math.Ceil(float64(float32(float32(r.AnimationMS) / combined)))
	if finish <= 0 || finish > math.MaxInt32 {
		return 0, 0, ErrAmmoRules
	}
	return time.Duration(refill) * time.Millisecond, time.Duration(finish) * time.Millisecond, nil
}

// NewEquippedAmmo applies original float32 capacity multiplication/truncation
// and schedules refill/completion from authored timing at the equipped speed.
func NewEquippedAmmo(weapon Weapon, modifiers WeaponModifiers) (*Ammo, error) {
	if weapon.ReserveAmmo < -1 || weapon.MagazineAmmo <= 0 || !nonnegative(modifiers.AmmoCapacity) || modifiers.AmmoCapacity <= 0 {
		return nil, ErrAmmoRules
	}
	refill, finish, err := weapon.BaseReload.schedule(modifiers.ReloadSpeed)
	if err != nil {
		return nil, err
	}
	reserve, magazine := float32(weapon.ReserveAmmo)*modifiers.AmmoCapacity, float32(weapon.MagazineAmmo)*modifiers.AmmoCapacity
	if reserve >= float32(math.MaxInt32) || magazine >= float32(math.MaxInt32) || magazine < 1 {
		return nil, ErrAmmoRules
	}
	// Original infinite reserve is the -1 sentinel, never a finite grant.
	if weapon.ReserveAmmo == -1 {
		reserve = -1
	}
	a := &Ammo{reserveLimit: int32(reserve), magazineLimit: int32(magazine), refillDelay: refill, finishDelay: finish}
	a.Reset()
	return a, nil
}

func (a *Ammo) Snapshot() AmmoSnapshot { return AmmoSnapshot{a.reserve, a.magazine, a.reloading} }

// Reset mirrors ResetWeapon: restore the reserve pool, then refill only the
// missing magazine rounds. A newly constructed pool starts with an empty clip;
// reserve capacity must not be granted in addition to a fresh full magazine.
func (a *Ammo) Reset() {
	a.reserve = a.reserveLimit
	a.reloading, a.refilled = false, false
	a.refillAt, a.finishAt = time.Time{}, time.Time{}
	a.refill()
}

func (a *Ammo) refill() {
	missing := a.magazineLimit - a.magazine
	if a.reserve != -1 {
		missing = min(missing, a.reserve)
		a.reserve -= missing
	}
	a.magazine += missing
}

// Consume spends one already-authorized discharge, independent of pellet count.
// It does not enforce rate of fire or turn an untrusted packet into a shot.
func (a *Ammo) Consume(now time.Time) bool {
	a.Advance(now)
	if a.reloading || a.magazine == 0 {
		return false
	}
	a.magazine--
	return true
}

func (a *Ammo) StartReload(now time.Time) bool {
	a.Advance(now)
	if a.reloading || a.magazine == a.magazineLimit || a.reserve == 0 {
		return false
	}
	a.reloading, a.refilled = true, false
	a.refillAt = now.Add(a.refillDelay)
	a.finishAt = now.Add(a.finishDelay)
	a.Advance(now)
	return true
}

func (a *Ammo) Advance(now time.Time) {
	if !a.reloading {
		return
	}
	if !a.refilled && !now.Before(a.refillAt) {
		a.refill()
		a.refilled = true
	}
	if !now.Before(a.finishAt) {
		a.reloading = false
	}
}

// Cancelling after the refill event retains the rounds already transferred.
func (a *Ammo) CancelReload(now time.Time) { a.Advance(now); a.reloading = false }

// GiveReserve and GiveMagazine are grants from trusted server rules only.
// Negative inputs cannot spend ammunition or overflow into a larger grant.
func (a *Ammo) GiveReserve(rounds int32) int32 {
	if rounds <= 0 || a.reserve == -1 {
		return 0
	}
	added := min(rounds, a.reserveLimit-a.reserve)
	a.reserve += added
	return added
}

// Pickup ammunition extends the existing total pool. A loaded magazine does
// not create additional capacity above the original maximum total ammunition.
func (a *Ammo) GiveTotal(rounds int32, now time.Time) int32 {
	a.Advance(now)
	if rounds <= 0 || a.reserve == -1 {
		return 0
	}
	added := min(rounds, max(int32(0), a.reserveLimit-a.reserve-a.magazine))
	a.reserve += added
	return added
}

func (a *Ammo) GiveMagazine(rounds int32) int32 {
	if rounds <= 0 {
		return 0
	}
	added := min(rounds, a.magazineLimit-a.magazine)
	a.magazine += added
	return added
}

// Native RefillWeapons sets the raw authored reserve cap on every held gun,
// preserving its loaded magazine and performing DoReload only if it is empty.
func (a *Ammo) RefillReserve(rounds int32) {
	if a.reserveLimit == -1 {
		a.reserve = -1
	} else {
		a.reserve = min(max(rounds, 0), a.reserveLimit)
	}
	if a.magazine == 0 {
		a.refill()
	}
}
