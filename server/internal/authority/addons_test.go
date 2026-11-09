package authority

import "testing"

func TestWeaponAddonsNativeConsumerCases(t *testing.T) {
	// Same synthetic tuning and cases executed by native-world-reader-tests.
	a := WeaponAddons{20, 25, .5, 40, 50, 75}
	for _, mask := range []uint{0, 2, 4, 8, 16, 32, 64, 126} {
		var equipped []byte
		want := WeaponModifiers{1, 1, 1, 1, 1, 1, false}
		if mask&2 != 0 {
			equipped = append(equipped, AddonDamage)
			want.Damage = 1.2
		}
		if mask&4 != 0 {
			equipped = append(equipped, AddonFireSpeed)
			want.FireSpeed = 1.25
			want.LauncherDelay = .5
		}
		if mask&8 != 0 {
			equipped = append(equipped, AddonAccuracy)
			want.Spread = .6
		}
		if mask&16 != 0 {
			equipped = append(equipped, AddonReload)
			want.ReloadSpeed = 1.5
		}
		if mask&32 != 0 {
			equipped = append(equipped, AddonAmmo)
			want.AmmoCapacity = 1.75
		}
		if mask&64 != 0 {
			equipped = append(equipped, AddonSuppressor)
			want.Suppressed = true
		}
		got, err := a.Modifiers(equipped...)
		if err != nil || got != want {
			t.Fatal(mask, got, want, err)
		}
	}
	one, _ := a.Modifiers(AddonDamage)
	repeated, _ := a.Modifiers(AddonDamage, AddonDamage)
	if repeated != one {
		t.Fatal("duplicate addon stacked")
	}
	if _, err := a.Modifiers(7); err == nil {
		t.Fatal("unknown addon accepted")
	}
}
