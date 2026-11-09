package authority

import (
	"encoding/json"
	"math"
	"os"
	"strings"
	"testing"
)

func fixture() document {
	return document{13, "synthetic-v1", strings.Repeat("a", 64), strings.Repeat("b", 64), Player{125, PlayerBody{.5, 2, 0, 0, 0}, 20}, WeaponAddons{20, 25, .5, 40, 50, 75}, Scoring{7, 9, 40, 25, 10},
		[]Weapon{{1, 42, 80, 20, 1, 1.25, 100, 125, Reload{400, 700, 1}, []FireMode{{2, true, 200, 150}, {3, false, 300, 250}}, 0, false}},
		[]Projectile{{TypeID: 2, Damage: DistanceDamage{30, 10, 5, 20}, Speed: 800, VehicleDamagePercent: 50, Bullet: true}},
		[]Grenade{{TypeID: 3, TimeToLiveMS: 2000, DamageRadius: 7}}, nil, []Equipment{}, GrenadePhysics{}, Melee{}, []Mount{}, []Weapon{}, VehicleImpact{}, nil, nil, nil, PickupRules{}, nil}
}

func encoded(t *testing.T, source document) []byte {
	t.Helper()
	data, err := json.Marshal(source)
	if err != nil {
		t.Fatal(err)
	}
	return data
}

func TestDefinitionsAreOwnedAndImmutable(t *testing.T) {
	data := encoded(t, fixture())
	d, err := Decode(data)
	if err != nil {
		t.Fatal(err)
	}
	clear(data)
	w, ok := d.Weapon(1)
	if !ok {
		t.Fatal("weapon missing")
	}
	w.FireModes[0].ProjectileTypeID = 999
	w.ReserveAmmo = 999
	w, _ = d.Weapon(1)
	if w.FireModes[0].ProjectileTypeID != 2 || w.ReserveAmmo != 80 {
		t.Fatal("caller mutated shared definitions")
	}
	if d.Version() != "synthetic-v1" || len(d.SHA256()) != 64 {
		t.Fatal("missing content identity")
	}
	if v, ok := d.WireWeapon(42); !ok || v.TypeID != 1 {
		t.Fatal("wire table", v, ok)
	}
	if _, ok := d.WireWeapon(1); ok {
		t.Fatal("type ID used as a wire index")
	}
	p := d.Player()
	p.BaseHealth = 999
	if d.Player().BaseHealth != 125 {
		t.Fatal("mutable player parameters")
	}
}

func TestInvalidAuthorityRejected(t *testing.T) {
	for name, mutate := range map[string]func(*document){
		"version":                    func(d *document) { d.SchemaVersion++ },
		"zero health":                func(d *document) { d.Player.BaseHealth = 0 },
		"negative spawn clearance":   func(d *document) { d.Player.FFASpawnClearance = -1 },
		"negative explosion damage":  func(d *document) { d.Projectiles[0].ExplosionDamage = -1 },
		"unbounded explosion":        func(d *document) { d.Projectiles[0].ExplosionDamage = 10 },
		"zero body radius":           func(d *document) { d.Player.Body.Radius = 0 },
		"short body":                 func(d *document) { d.Player.Body.Height = .5 },
		"zero addon delay":           func(d *document) { d.WeaponAddons.LauncherDelayMultiplier = 0 },
		"negative addon":             func(d *document) { d.WeaponAddons.AmmoCapacityPercent = -1 },
		"negative spread":            func(d *document) { d.WeaponAddons.SpreadReductionPercent = 101 },
		"zero reload":                func(d *document) { d.Weapons[0].BaseReload.AnimationMS = 0 },
		"negative projectile count":  func(d *document) { d.Weapons[0].ProjectilesPerShot = -1 },
		"excessive projectile count": func(d *document) { d.Weapons[0].ProjectilesPerShot = 257 },
		"negative weapon damage":     func(d *document) { d.Weapons[0].BaseDamageMultiplier = -1 },
		"late refill":                func(d *document) { d.Weapons[0].BaseReload.EventMS = 701 },
		"negative health":            func(d *document) { d.Player.BaseHealth = -1 },
		"duplicate wire index":       func(d *document) { w := d.Weapons[0]; w.TypeID = 999; d.Weapons = append(d.Weapons, w) },
		"identity":                   func(d *document) { d.LibrarySHA256 = "bad" },
		"duplicate weapon":           func(d *document) { d.Weapons = append(d.Weapons, d.Weapons[0]) },
		"duplicate projectile":       func(d *document) { d.Projectiles = append(d.Projectiles, d.Projectiles[0]) },
		"duplicate grenade":          func(d *document) { d.Grenades = append(d.Grenades, d.Grenades[0]) },
		"ambiguous shot":             func(d *document) { d.Grenades[0].TypeID = 2 },
		"dangling projectile":        func(d *document) { d.Weapons[0].FireModes[0].ProjectileTypeID = 999 },
		"empty magazine":             func(d *document) { d.Weapons[0].MagazineAmmo = 0 },
		"negative reserve":           func(d *document) { d.Weapons[0].ReserveAmmo = -2 },
		"negative delay":             func(d *document) { d.Weapons[0].FireModes[0].ContinuousDelayMS = -1 },
		"negative fire delay":        func(d *document) { d.Weapons[0].FireModes[0].BaseFireDelayMS = -1 },
		"negative damage":            func(d *document) { d.Projectiles[0].Damage.NearDamage = -1 },
		"reversed curve":             func(d *document) { d.Projectiles[0].Damage.FarDistance = 2 },
		"negative speed":             func(d *document) { d.Projectiles[0].Speed = -1 },
		"negative fuse":              func(d *document) { d.Grenades[0].TimeToLiveMS = -1 },
	} {
		t.Run(name, func(t *testing.T) {
			source := fixture()
			mutate(&source)
			if _, err := Decode(encoded(t, source)); err == nil {
				t.Fatal("invalid definition accepted")
			}
		})
	}
	valid := string(encoded(t, fixture()))
	for name, data := range map[string]string{
		"unknown":   strings.Replace(valid, `"schema_version":13`, `"extra":0,"schema_version":13`, 1),
		"duplicate": strings.Replace(valid, `"schema_version":13`, `"schema_version":13,"schema_version":13`, 1),
		"alias":     strings.Replace(valid, `"schema_version"`, `"SCHEMA_VERSION"`, 1),
		"missing":   strings.Replace(valid, `,"sticky":false`, ``, 1),
		"null":      strings.Replace(valid, `"sticky":false`, `"sticky":null`, 1),
		"trailing":  valid + "{}",
		"overflow":  strings.Replace(valid, `"speed_world_units_per_second":800`, `"speed_world_units_per_second":1e100`, 1),
		"truncated": valid[:len(valid)-1],
		"oversized": strings.Repeat(" ", MaxBytes+1),
	} {
		t.Run(name, func(t *testing.T) {
			if _, err := Decode([]byte(data)); err == nil {
				t.Fatal("malformed data accepted")
			}
		})
	}
}

func TestDirectDamageNativeCases(t *testing.T) {
	p := fixture().Projectiles[0]
	for _, row := range []struct{ distance, want float32 }{{0, 30}, {5, 30}, {12.5, 20}, {20, 10}, {30, 10}} {
		for _, continuous := range []bool{false, true} {
			p.DamagePerSecond = continuous
			want := row.want
			if continuous {
				want *= .25
			}
			got, err := p.DirectDamage(row.distance, 250, 1)
			if err != nil || got != want {
				t.Fatal(row, continuous, got, err)
			}
		}
	}
	p.Damage = DistanceDamage{12, 12, 0, 0}
	p.DamagePerSecond = false
	for _, distance := range []float32{0, 10, 100} {
		if got, err := p.DirectDamage(distance, 0, 2); err != nil || got != 24 {
			t.Fatal(got, err)
		}
	}
	for _, bad := range []float32{-1, float32(math.NaN()), float32(math.Inf(1))} {
		if _, err := p.DirectDamage(bad, 1, 1); err == nil {
			t.Fatal("invalid distance accepted")
		}
		if _, err := p.DirectDamage(0, 1, bad); err == nil {
			t.Fatal("invalid modifier accepted")
		}
	}
}

func TestDistributedNumericData(t *testing.T) {
	data, err := os.ReadFile("../../data/authority.json")
	if err != nil {
		t.Fatal(err)
	}
	d, err := Decode(data)
	if err != nil {
		t.Fatal(err)
	}
	if len(d.weapons) != 10 || len(d.projectiles) != 13 || len(d.grenades) != 12 {
		t.Fatal("unexpected catalogue coverage")
	}
}
