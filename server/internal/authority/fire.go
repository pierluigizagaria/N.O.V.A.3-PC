package authority

import (
	"errors"
	"math"
	"time"
)

// FireInterval is a discharge admission envelope derived from authored timing
// and the original native cooldown. It does not drive client animation.
func (w Weapon) FireInterval(mode int, modifiers WeaponModifiers) (time.Duration, error) {
	if mode < 0 || mode >= len(w.FireModes) || !nonnegative(w.MinimumFireIntervalMS, modifiers.FireSpeed, modifiers.LauncherDelay) ||
		w.MinimumFireIntervalMS <= 0 || modifiers.FireSpeed <= 0 || modifiers.LauncherDelay <= 0 {
		return 0, errors.New("invalid fire interval rules")
	}
	interval := float32(w.MinimumFireIntervalMS / modifiers.FireSpeed)
	cooldown := float32(float32(w.FireModes[mode].BaseFireDelayMS) / modifiers.FireSpeed)
	// Native FireWeapon singles out this protocol weapon identity; the delay
	// and equipment multiplier remain versioned authority parameters.
	if w.TypeID == 23092 {
		cooldown = float32(float32(w.FireModes[mode].BaseFireDelayMS) * modifiers.LauncherDelay)
	}
	interval = max(interval, cooldown)
	if !nonnegative(interval) || interval <= 0 || interval >= math.MaxInt32 {
		return 0, errors.New("invalid equipped fire interval")
	}
	return time.Duration(math.Ceil(float64(interval) * float64(time.Millisecond))), nil
}

// FireGate permits two discharges to arrive together, accommodating UDP batching
// without accumulating an idle-time stockpile. Only server receipt time refills
// this budget. The caller commits after all other authorization checks pass.
type FireGate struct{ next time.Time }

func (g FireGate) Allows(now time.Time, interval time.Duration) bool {
	return interval > 0 && !now.Before(g.next.Add(-interval))
}

func (g *FireGate) Commit(now time.Time, interval time.Duration) {
	if g.next.Before(now) {
		g.next = now
	}
	g.next = g.next.Add(interval)
}
