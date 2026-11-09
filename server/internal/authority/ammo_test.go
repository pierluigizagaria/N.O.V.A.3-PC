package authority

import (
	"math"
	"testing"
	"time"
)

func ammoFixture(t *testing.T, reserve, magazine int32) *Ammo {
	t.Helper()
	a, err := NewAmmo(Weapon{ReserveAmmo: reserve, MagazineAmmo: magazine, BaseReload: Reload{EventMS: 400, AnimationMS: 700, AnimationSpeed: 1}})
	if err != nil {
		t.Fatal(err)
	}
	return a
}

func TestEquippedAmmoCapacityAndRawReloadTiming(t *testing.T) {
	w := Weapon{ReserveAmmo: 80, MagazineAmmo: 20, BaseReload: Reload{550, 666, .8}}
	now := time.Unix(100, 0)
	for _, tc := range []struct {
		speed          float32
		refill, finish int64
	}{{1, 688, 833}, {1.25, 550, 666}} {
		a, err := NewEquippedAmmo(w, WeaponModifiers{AmmoCapacity: 1.75, ReloadSpeed: tc.speed})
		if err != nil || a.Snapshot() != (AmmoSnapshot{105, 35, false}) {
			t.Fatal(a, err)
		}
		a.Consume(now)
		if !a.StartReload(now) {
			t.Fatal("reload refused")
		}
		a.Advance(now.Add(time.Duration(tc.refill-1) * time.Millisecond))
		if a.Snapshot() != (AmmoSnapshot{105, 34, true}) {
			t.Fatal("early refill", a.Snapshot())
		}
		a.Advance(now.Add(time.Duration(tc.refill) * time.Millisecond))
		if a.Snapshot() != (AmmoSnapshot{104, 35, true}) {
			t.Fatal("wrong refill", a.Snapshot())
		}
		a.Advance(now.Add(time.Duration(tc.finish-1) * time.Millisecond))
		if !a.Snapshot().Reloading {
			t.Fatal("early completion")
		}
		a.Advance(now.Add(time.Duration(tc.finish) * time.Millisecond))
		if a.Snapshot().Reloading {
			t.Fatal("late completion")
		}
	}
	for _, speed := range []float32{0, -1, float32(math.NaN()), float32(math.Inf(1)), 1e-30} {
		if _, err := NewEquippedAmmo(w, WeaponModifiers{AmmoCapacity: 1, ReloadSpeed: speed}); err == nil {
			t.Fatal("invalid speed accepted", speed)
		}
		if _, err := NewEquippedAmmo(w, WeaponModifiers{AmmoCapacity: speed, ReloadSpeed: 1}); err == nil {
			t.Fatal("invalid capacity accepted", speed)
		}
	}
	w.ReserveAmmo = math.MaxInt32
	if _, err := NewEquippedAmmo(w, WeaponModifiers{AmmoCapacity: 2, ReloadSpeed: 1}); err == nil {
		t.Fatal("overflow accepted")
	}
}

func TestAmmoSpawnResetAndRefillConserveNativePool(t *testing.T) {
	now := time.Unix(100, 0)
	for _, tc := range []struct{ reserve, magazine, wantReserve, wantMagazine int32 }{{80, 20, 60, 20}, {5, 20, 0, 5}, {0, 20, 0, 0}, {-1, 20, -1, 20}} {
		a := ammoFixture(t, tc.reserve, tc.magazine)
		if a.Snapshot() != (AmmoSnapshot{tc.wantReserve, tc.wantMagazine, false}) {
			t.Fatal(a.Snapshot())
		}
	}
	a := ammoFixture(t, 80, 20)
	for range 16 {
		if !a.Consume(now) {
			t.Fatal("early empty magazine")
		}
	}
	a.Reset()
	if a.Snapshot() != (AmmoSnapshot{64, 20, false}) {
		t.Fatal("native ResetWeapon refills only missing rounds", a.Snapshot())
	}
	if a.GiveReserve(math.MaxInt32) != 16 || a.GiveReserve(-1) != 0 || a.reserve != 80 {
		t.Fatal("reserve cap/overflow")
	}
	for range 4 {
		a.Consume(now)
	}
	if a.GiveMagazine(10) != 4 || a.GiveMagazine(-1) != 0 || a.magazine != 20 {
		t.Fatal("magazine cap")
	}
}

func TestAmmoReloadEventAndCompletionAreSeparate(t *testing.T) {
	now := time.Unix(100, 0)
	a := ammoFixture(t, 80, 20)
	for range 20 {
		if !a.Consume(now) {
			t.Fatal("missing starting round")
		}
	}
	if a.Consume(now) || !a.StartReload(now) || a.StartReload(now) {
		t.Fatal("empty/reload admission")
	}
	a.Advance(now.Add(399 * time.Millisecond))
	if a.Snapshot() != (AmmoSnapshot{60, 0, true}) {
		t.Fatal("refilled early")
	}
	a.Advance(now.Add(400 * time.Millisecond))
	if a.Snapshot() != (AmmoSnapshot{40, 20, true}) || a.Consume(now.Add(699*time.Millisecond)) {
		t.Fatal("fired before reload completion")
	}
	if !a.Consume(now.Add(700*time.Millisecond)) || a.Snapshot() != (AmmoSnapshot{40, 19, false}) {
		t.Fatal("reload completion")
	}
	a.Advance(now.Add(time.Hour))
	if a.reserve != 40 || a.magazine != 19 {
		t.Fatal("repeated refill")
	}
}

func TestAmmoReloadCancellationAndInfiniteReserve(t *testing.T) {
	now := time.Unix(100, 0)
	for _, after := range []time.Duration{399 * time.Millisecond, 400 * time.Millisecond} {
		a := ammoFixture(t, 80, 20)
		for range 20 {
			a.Consume(now)
		}
		a.StartReload(now)
		a.CancelReload(now.Add(after))
		a.Advance(now.Add(time.Hour))
		want := AmmoSnapshot{60, 0, false}
		if after == 400*time.Millisecond {
			want = AmmoSnapshot{40, 20, false}
		}
		if a.Snapshot() != want {
			t.Fatal("cancellation changed transferred rounds", a.Snapshot())
		}
	}
	a := ammoFixture(t, -1, 3)
	for round := 0; round < 3; round++ {
		for range 3 {
			if !a.Consume(now) {
				t.Fatal("infinite reserve lost")
			}
		}
		if !a.StartReload(now) {
			t.Fatal("infinite reserve denied reload")
		}
		now = now.Add(time.Second)
		a.Advance(now)
		if a.Snapshot() != (AmmoSnapshot{-1, 3, false}) {
			t.Fatal(a.Snapshot())
		}
	}
}

func TestAmmoRejectsInvalidRules(t *testing.T) {
	for _, w := range []Weapon{
		{}, {ReserveAmmo: -2, MagazineAmmo: 1, BaseReload: Reload{AnimationMS: 1, AnimationSpeed: 1}},
		{MagazineAmmo: 1, BaseReload: Reload{EventMS: 2, AnimationMS: 1, AnimationSpeed: 1}},
		{MagazineAmmo: 1, BaseReload: Reload{EventMS: -1, AnimationMS: 1, AnimationSpeed: 1}},
	} {
		if _, err := NewAmmo(w); err == nil {
			t.Fatal("invalid ammo rules")
		}
	}
}
