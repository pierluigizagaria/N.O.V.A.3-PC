package authority

import (
	"errors"
	"math"
	"slices"
)

type PickupChoice struct {
	WeaponTypeID int32   `json:"weapon_type_id"`
	PowerTypeID  int32   `json:"power_type_id"`
	Ammo         int32   `json:"ammo"`
	AmmoOnly     bool    `json:"ammo_only"`
	Radius       float32 `json:"radius_world_units"`
	RespawnMS    int32   `json:"respawn_ms"`
}

type Pickup struct {
	Map           byte           `json:"map"`
	ObjectID      int32          `json:"object_id"`
	Zone          int32          `json:"zone"`
	Position      [3]float32     `json:"position"`
	Enabled       bool           `json:"enabled"`
	Modes         int32          `json:"modes"`
	Choices       []PickupChoice `json:"choices"`
	InitialChoice byte           `json:"initial_choice"`
}

type SpawnZone struct {
	Map     byte   `json:"map"`
	SpawnID uint32 `json:"spawn_id"`
	Zone    int32  `json:"zone"`
}

type PickupRules struct {
	FirstDynamicObjectID int32       `json:"first_dynamic_object_id"`
	DynamicObjectLimit   int32       `json:"dynamic_object_limit"`
	DropLifetimeMS       int32       `json:"drop_lifetime_ms"`
	DropGroundOffset     float32     `json:"drop_ground_offset"`
	DropGroundDistance   float32     `json:"drop_ground_distance"`
	SpawnZones           []SpawnZone `json:"spawn_zones"`
}

func (d *Definitions) Pickups(mapID byte) []Pickup {
	var out []Pickup
	for _, p := range d.pickups {
		if p.Map == mapID && p.Enabled {
			p.Choices = slices.Clone(p.Choices)
			out = append(out, p)
		}
	}
	return out
}

func (d *Definitions) WeaponDrop(id int32) (PickupChoice, bool) {
	p, ok := d.weaponDrops[id]
	return p, ok
}
func (d *Definitions) PickupRules() PickupRules {
	r := d.pickupRules
	r.SpawnZones = slices.Clone(r.SpawnZones)
	return r
}
func (d *Definitions) SpawnZone(mapID byte, spawn uint32) (int32, bool) {
	for _, z := range d.pickupRules.SpawnZones {
		if z.Map == mapID && z.SpawnID == spawn {
			return z.Zone, true
		}
	}
	return 0, false
}

func (d *Definitions) loadPickups(source document) error {
	r := source.PickupRules
	// Empty synthetic definitions do not create an alternative runtime path.
	if len(source.Pickups)+len(source.WeaponDrops) > 0 && (r.FirstDynamicObjectID <= 0 || r.DynamicObjectLimit <= r.FirstDynamicObjectID || r.DynamicObjectLimit > math.MaxInt32 || r.DropLifetimeMS <= 0 || !nonnegative(r.DropGroundOffset, r.DropGroundDistance) || r.DropGroundDistance == 0) {
		return errors.New("invalid pickup lifecycle rules")
	}
	d.pickupRules = r
	d.pickupRules.SpawnZones = slices.Clone(r.SpawnZones)
	seenSpawns := make(map[[2]uint32]bool)
	for _, z := range r.SpawnZones {
		key := [2]uint32{uint32(z.Map), z.SpawnID}
		if z.Map > 5 || z.Zone < 0 || seenSpawns[key] {
			return errors.New("invalid spawn zone")
		}
		seenSpawns[key] = true
	}
	d.weaponDrops = make(map[int32]PickupChoice)
	valid := func(c PickupChoice) bool {
		return c.WeaponTypeID > 0 && c.WeaponTypeID <= math.MaxUint16 && c.PowerTypeID == 0 && c.Ammo >= 0 && c.Ammo <= 255 && c.RespawnMS >= -1000 && nonnegative(c.Radius) && c.Radius > 0
	}
	for _, c := range source.WeaponDrops {
		if _, duplicate := d.weaponDrops[c.WeaponTypeID]; duplicate || !valid(c) {
			return errors.New("invalid weapon drop")
		}
		d.weaponDrops[c.WeaponTypeID] = c
	}
	seen := make(map[[2]int32]bool)
	for _, p := range source.Pickups {
		key := [2]int32{int32(p.Map), p.ObjectID}
		if p.Map > 5 || p.ObjectID <= 0 || p.ObjectID >= r.FirstDynamicObjectID || p.Zone < 0 || len(p.Choices) == 0 || len(p.Choices) > 255 || int(p.InitialChoice) >= len(p.Choices) || seen[key] {
			return errors.New("invalid map pickup")
		}
		seen[key] = true
		for _, v := range p.Position {
			if !nonnegative(float32(math.Abs(float64(v)))) {
				return errors.New("invalid pickup placement")
			}
		}
		for _, c := range p.Choices {
			if !valid(c) {
				return errors.New("invalid pickup choice")
			}
		}
		p.Choices = slices.Clone(p.Choices)
		d.pickups = append(d.pickups, p)
	}
	return nil
}
