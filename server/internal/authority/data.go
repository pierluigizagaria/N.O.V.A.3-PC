// Package authority loads the server-owned numeric rules independently of the
// original game. Definitions are immutable for the lifetime of a server run.
package authority

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math"
	"reflect"
	"slices"

	"nova3.test/backend/internal/collision"
)

const MaxBytes = 8 << 20

// Distances use the same world units as collision maps and player coordinates.
// A zero-width falloff is valid: native weapons may have constant damage.
type DistanceDamage struct {
	NearDamage   float32 `json:"near_damage"`
	FarDamage    float32 `json:"far_damage"`
	NearDistance float32 `json:"near_distance_world_units"`
	FarDistance  float32 `json:"far_distance_world_units"`
}

type Projectile struct {
	TypeID               int32          `json:"type_id"`
	Damage               DistanceDamage `json:"damage"`
	DamagePerSecond      bool           `json:"damage_per_second"`
	Speed                float32        `json:"speed_world_units_per_second"`
	VehicleDamagePercent float32        `json:"vehicle_damage_percent"`
	Bullet               bool           `json:"bullet"`
	Ray                  bool           `json:"ray"`
	Railgun              bool           `json:"railgun"`
	ExplosionDamage      float32        `json:"explosion_damage"`
	ExplosionRadius      float32        `json:"explosion_radius_world_units"`
	ContactRadius        float32        `json:"contact_radius_world_units"`
	RayLength            float32        `json:"ray_length_world_units"`
	ActiveMS             int32          `json:"active_time_ms"`
	LifetimeMS           int32          `json:"lifetime_ms"`
}

type Grenade struct {
	TypeID           int32   `json:"type_id"`
	TimeToLiveMS     int32   `json:"time_to_live_ms"`
	DamageRadius     float32 `json:"damage_radius_world_units"`
	Sticky           bool    `json:"sticky"`
	WireIndex        uint8   `json:"wire_index"`
	Damage           float32 `json:"damage"`
	Speed            float32 `json:"speed_world_units_per_second"`
	Drag             float32 `json:"drag"`
	ContactRadius    float32 `json:"contact_radius_world_units"`
	Effect           int32   `json:"effect"`
	EffectDurationMS int32   `json:"effect_duration_ms"`
	EffectStrength   float32 `json:"effect_strength"`
}

type Equipment struct {
	TypeID          int32 `json:"type_id"`
	GrenadeTypeID   int32 `json:"grenade_type_id"`
	Stock           int32 `json:"stock"`
	WeaponTypeID    int32 `json:"weapon_type_id"`
	WeaponWireIndex byte  `json:"weapon_wire_index"`
}
type Melee struct {
	Range               float32 `json:"range_world_units"`
	Damage              float32 `json:"damage"`
	CooldownMS          int32   `json:"cooldown_ms"`
	TargetRange         float32 `json:"target_range_world_units"`
	MinimumForwardDot   float32 `json:"minimum_forward_dot"`
	SightHeightFraction float32 `json:"sight_height_fraction"`
}
type Mount struct {
	Map              byte                    `json:"map"`
	Category         byte                    `json:"category"`
	WireIndex        byte                    `json:"wire_index"`
	ObjectID         uint32                  `json:"object_id"`
	TypeID           int32                   `json:"type_id"`
	Position         [3]float32              `json:"position"`
	Rotation         [3]float32              `json:"rotation"`
	Enabled          bool                    `json:"enabled"`
	Modes            int32                   `json:"modes"`
	Health           float32                 `json:"health"`
	Radius           float32                 `json:"radius_world_units"`
	Height           float32                 `json:"height_world_units"`
	WeaponTypeID     int32                   `json:"weapon_type_id"`
	StaticWireIndex  byte                    `json:"static_wire_index"`
	ProxyWireIndices [2]byte                 `json:"proxy_wire_indices"`
	RespawnMS        int32                   `json:"respawn_ms"`
	Capsules         []MountCapsule          `json:"capsules"`
	MaximumSpeedKMH  float32                 `json:"maximum_speed_kmh"`
	Gunner           *GunnerAnchor           `json:"gunner"`
	Occupants        *CarOccupants           `json:"occupants"`
	Interaction      []collision.BoundRegion `json:"interaction"`
}
type CarOccupants struct {
	DriverPosition  [3]float32     `json:"driver_position"`
	ShooterPivot    [3]float32     `json:"shooter_pivot"`
	ShooterOffset   [3]float32     `json:"shooter_offset"`
	DriverCapsules  []MountCapsule `json:"driver_capsules"`
	ShooterCapsules []MountCapsule `json:"shooter_capsules"`
}
type GunnerAnchor struct {
	Pivot    [3]float32 `json:"pivot"`
	Seat     [3]float32 `json:"seat"`
	Rotation [3]float32 `json:"rotation"`
}
type VehicleImpact struct {
	MinimumHealthFraction float32 `json:"minimum_health_fraction"`
	MaximumHealthFraction float32 `json:"maximum_health_fraction"`
	DamageMultiplier      float32 `json:"damage_multiplier"`
}
type MountCapsule struct {
	A                [3]float32 `json:"a"`
	B                [3]float32 `json:"b"`
	Radius           float32    `json:"radius"`
	DamageMultiplier float32    `json:"damage_multiplier"`
}
type GrenadePhysics struct {
	Gravity          float32 `json:"gravity_world_units_per_second_squared"`
	BounceRetention  float32 `json:"bounce_retention"`
	SurfaceClearance float32 `json:"surface_clearance_world_units"`
	BodyRadius       float32 `json:"body_radius_world_units"`
	StopSpeedSquared float32 `json:"stop_speed_squared"`
}

type FireMode struct {
	ProjectileTypeID  int32 `json:"projectile_type_id"`
	Continuous        bool  `json:"continuous"`
	ContinuousDelayMS int32 `json:"continuous_delay_ms"`
	BaseFireDelayMS   int32 `json:"base_fire_delay_ms"`
}

type Weapon struct {
	TypeID                int32      `json:"type_id"`
	WireIndex             uint8      `json:"wire_index"`
	ReserveAmmo           int32      `json:"reserve_ammo"`
	MagazineAmmo          int32      `json:"magazine_ammo"`
	ProjectilesPerShot    int32      `json:"projectiles_per_shot"`
	BaseDamageMultiplier  float32    `json:"base_damage_multiplier"`
	Range                 float32    `json:"range_world_units"`
	MinimumFireIntervalMS float32    `json:"minimum_fire_interval_ms"`
	BaseReload            Reload     `json:"base_reload"`
	FireModes             []FireMode `json:"fire_modes"`
	SpreadPattern         int32      `json:"spread_pattern"`
	SpreadSuppressed      bool       `json:"spread_suppressed"`
}

// Authored numeric timing before speed division or rounding. Keeping the raw
// event and clip duration avoids rounding twice when equipment changes speed.
type Reload struct {
	EventMS        int32   `json:"event_ms"`
	AnimationMS    int32   `json:"animation_ms"`
	AnimationSpeed float32 `json:"animation_speed"`
}

// BaseHealth precedes equipment/perk modifiers; it is not a complete health rule.
type Player struct {
	BaseHealth        float32    `json:"base_health"`
	Body              PlayerBody `json:"body"`
	FFASpawnClearance float32    `json:"ffa_spawn_clearance_world_units"`
}

// The original on-foot collision capsule, in Z-up coordinates. Height includes
// both caps; Offset is added to the player's foot position. This does not
// describe projectile targets: native ray filters exclude this actor collider.
type PlayerBody struct {
	Radius  float32 `json:"radius_world_units"`
	Height  float32 `json:"height_world_units"`
	OffsetX float32 `json:"offset_x_world_units"`
	OffsetY float32 `json:"offset_y_world_units"`
	OffsetZ float32 `json:"offset_z_world_units"`
}

// Shared local/remote weapon add-on tuning. Perk parameters are separate.
type WeaponAddons struct {
	DamagePercent           int32   `json:"damage_percent"`
	FireSpeedPercent        int32   `json:"fire_speed_percent"`
	LauncherDelayMultiplier float32 `json:"launcher_delay_multiplier"`
	SpreadReductionPercent  int32   `json:"spread_reduction_percent"`
	ReloadSpeedPercent      int32   `json:"reload_speed_percent"`
	AmmoCapacityPercent     int32   `json:"ammo_capacity_percent"`
}

type document struct {
	SchemaVersion  uint32               `json:"schema_version"`
	ContentVersion string               `json:"content_version"`
	LibrarySHA256  string               `json:"library_sha256"`
	WorldSHA256    string               `json:"world_sha256"`
	Player         Player               `json:"player"`
	WeaponAddons   WeaponAddons         `json:"weapon_addons"`
	Scoring        Scoring              `json:"scoring"`
	Weapons        []Weapon             `json:"weapons"`
	Projectiles    []Projectile         `json:"projectiles"`
	Grenades       []Grenade            `json:"grenades"`
	Collision      *collision.Model     `json:"collision"`
	Equipment      []Equipment          `json:"equipment"`
	GrenadePhysics GrenadePhysics       `json:"grenade_physics"`
	Melee          Melee                `json:"melee"`
	Mounts         []Mount              `json:"mounts"`
	MountedWeapons []Weapon             `json:"mounted_weapons"`
	VehicleImpact  VehicleImpact        `json:"vehicle_impact"`
	MechCollision  *collision.MechModel `json:"mech_collision"`
	Pickups        []Pickup             `json:"pickups"`
	WeaponDrops    []PickupChoice       `json:"weapon_drops"`
	PickupRules    PickupRules          `json:"pickup_rules"`
	BurstFire      []Burst              `json:"burst_fire"`
	Consumables    Consumables          `json:"consumables"`
}

type Definitions struct {
	version, library, world, digest string
	weapons                         map[int32]Weapon
	projectiles                     map[int32]Projectile
	grenades                        map[int32]Grenade
	player                          Player
	addons                          WeaponAddons
	scoring                         Scoring
	wireWeapons                     map[uint8]int32
	collision                       *collision.Model
	equipment                       map[int32]Equipment
	wireGrenades                    map[uint8]int32
	grenadePhysics                  GrenadePhysics
	melee                           Melee
	mounts                          []Mount
	mountedWeapons                  map[int32]Weapon
	vehicleImpact                   VehicleImpact
	mechCollision                   *collision.MechModel
	pickups                         []Pickup
	weaponDrops                     map[int32]PickupChoice
	pickupRules                     PickupRules
	bursts                          map[[2]int32]Burst
	consumables                     Consumables
}

func (d *Definitions) MountedWeapon(id int32) (Weapon, bool) {
	w, ok := d.mountedWeapons[id]
	w.FireModes = slices.Clone(w.FireModes)
	return w, ok
}
func (d *Definitions) Melee() Melee                 { return d.melee }
func (d *Definitions) VehicleImpact() VehicleImpact { return d.vehicleImpact }
func (d *Definitions) Mounts(mapID byte) []Mount {
	var out []Mount
	for _, m := range d.mounts {
		if m.Map == mapID && m.Enabled {
			m.Capsules = slices.Clone(m.Capsules)
			m.Interaction = slices.Clone(m.Interaction)
			if m.Gunner != nil {
				copy := *m.Gunner
				m.Gunner = &copy
			}
			if m.Occupants != nil {
				copy := *m.Occupants
				copy.DriverCapsules = slices.Clone(copy.DriverCapsules)
				copy.ShooterCapsules = slices.Clone(copy.ShooterCapsules)
				m.Occupants = &copy
			}
			out = append(out, m)
		}
	}
	return out
}

func (d *Definitions) Equipment(id int32) (Equipment, bool) { e, ok := d.equipment[id]; return e, ok }
func (d *Definitions) WireGrenade(index uint8) (Grenade, bool) {
	id, ok := d.wireGrenades[index]
	if !ok {
		return Grenade{}, false
	}
	return d.Grenade(id)
}
func (d *Definitions) GrenadePhysics() GrenadePhysics { return d.grenadePhysics }

func (d *Definitions) Version() string            { return d.version }
func (d *Definitions) SHA256() string             { return d.digest }
func (d *Definitions) LibrarySHA256() string      { return d.library }
func (d *Definitions) WorldSHA256() string        { return d.world }
func (d *Definitions) Player() Player             { return d.player }
func (d *Definitions) WeaponAddons() WeaponAddons { return d.addons }
func (d *Definitions) Scoring() Scoring           { return d.scoring }

// Collision is immutable after decoding, like the mounted world geometry.
func (d *Definitions) Collision() *collision.Model         { return d.collision }
func (d *Definitions) MechCollision() *collision.MechModel { return d.mechCollision }
func (d *Definitions) WireWeapon(index uint8) (Weapon, bool) {
	id, ok := d.wireWeapons[index]
	if !ok {
		return Weapon{}, false
	}
	if w, ok := d.Weapon(id); ok {
		return w, true
	}
	return d.MountedWeapon(id)
}
func (d *Definitions) Weapon(id int32) (Weapon, bool) {
	w, ok := d.weapons[id]
	w.FireModes = slices.Clone(w.FireModes)
	return w, ok
}
func (d *Definitions) Projectile(id int32) (Projectile, bool) {
	p, ok := d.projectiles[id]
	return p, ok
}
func (d *Definitions) Grenade(id int32) (Grenade, bool) { g, ok := d.grenades[id]; return g, ok }

func Decode(data []byte) (*Definitions, error) {
	if len(data) == 0 || len(data) > MaxBytes {
		return nil, errors.New("authority data exceeds size limits")
	}
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.UseNumber()
	if err := shape(decoder, reflect.TypeFor[document]()); err != nil {
		return nil, fmt.Errorf("authority schema: %w", err)
	}
	if _, err := decoder.Token(); err != io.EOF {
		return nil, errors.New("authority data has trailing content")
	}
	var source document
	if err := json.Unmarshal(data, &source); err != nil {
		return nil, err
	}
	if source.SchemaVersion != 16 || len(source.ContentVersion) == 0 || len(source.ContentVersion) > 80 || !hash(source.LibrarySHA256) || !hash(source.WorldSHA256) {
		return nil, errors.New("invalid authority identity or schema version")
	}
	if len(source.Weapons) == 0 || len(source.Weapons) > 256 || len(source.Projectiles) == 0 || len(source.Projectiles) > 512 || len(source.Grenades) > 256 {
		return nil, errors.New("invalid authority definition counts")
	}
	sum := sha256.Sum256(data)
	if !nonnegative(source.Player.BaseHealth, source.Player.FFASpawnClearance) || source.Player.BaseHealth == 0 {
		return nil, errors.New("invalid player base health")
	}
	if !source.Consumables.valid() {
		return nil, errors.New("invalid consumable rules")
	}
	if !source.Scoring.valid() {
		return nil, errors.New("invalid scoreboard rules")
	}
	if source.Collision != nil {
		if err := source.Collision.Validate(); err != nil {
			return nil, err
		}
	}
	if source.MechCollision != nil {
		if err := source.MechCollision.Validate(); err != nil {
			return nil, err
		}
	}
	body := source.Player.Body
	if !nonnegative(body.Radius, body.Height) || body.Radius <= 0 || body.Height < 2*body.Radius ||
		!nonnegative(float32(math.Abs(float64(body.OffsetX))), float32(math.Abs(float64(body.OffsetY))), float32(math.Abs(float64(body.OffsetZ)))) {
		return nil, errors.New("invalid player collision capsule")
	}
	if !source.WeaponAddons.valid() {
		return nil, errors.New("invalid weapon addon parameters")
	}
	d := &Definitions{version: source.ContentVersion, library: source.LibrarySHA256, world: source.WorldSHA256, digest: hex.EncodeToString(sum[:]),
		weapons: make(map[int32]Weapon), projectiles: make(map[int32]Projectile), grenades: make(map[int32]Grenade),
		player: source.Player, addons: source.WeaponAddons, scoring: source.Scoring, wireWeapons: make(map[uint8]int32), collision: source.Collision}
	d.consumables = source.Consumables
	d.equipment = make(map[int32]Equipment)
	d.wireGrenades = make(map[uint8]int32)
	d.grenadePhysics = source.GrenadePhysics
	d.melee = source.Melee
	d.vehicleImpact = source.VehicleImpact
	d.mechCollision = source.MechCollision
	if err := d.loadPickups(source); err != nil {
		return nil, err
	}
	if !nonnegative(source.VehicleImpact.MinimumHealthFraction, source.VehicleImpact.MaximumHealthFraction, source.VehicleImpact.DamageMultiplier) || source.VehicleImpact.MaximumHealthFraction < source.VehicleImpact.MinimumHealthFraction {
		return nil, errors.New("invalid vehicle impact rules")
	}
	d.mountedWeapons = make(map[int32]Weapon)
	for _, m := range source.Mounts {
		if !validMountInteraction(m, source.MechCollision) {
			return nil, errors.New("invalid mount interaction volumes")
		}
		if m.Category == 1 {
			if m.Occupants == nil || !validCarOccupants(m.Occupants) {
				return nil, errors.New("invalid car occupant collision model")
			}
		} else if m.Occupants != nil {
			return nil, errors.New("unexpected car occupant collision model")
		}
		if m.Category == 3 && (source.Collision == nil || source.Collision.MachinegunAngleRange <= 0) {
			return nil, errors.New("missing mounted gun collision poses")
		}
		if m.Gunner != nil {
			for _, point := range [][3]float32{m.Gunner.Pivot, m.Gunner.Seat, m.Gunner.Rotation} {
				for _, v := range point {
					if !nonnegative(float32(math.Abs(float64(v)))) {
						return nil, errors.New("invalid gunner anchor")
					}
				}
			}
		} else if m.Category == 3 {
			return nil, errors.New("missing gunner anchor")
		}
		if m.Category == 2 && (source.MechCollision == nil || source.MechCollision.TypeID != m.TypeID) {
			return nil, errors.New("missing mech collision model")
		}
		if m.Map > 5 || m.Category < 1 || m.Category > 3 || !nonnegative(m.Health, m.Radius, m.Height, m.MaximumSpeedKMH) || m.Category == 1 && m.MaximumSpeedKMH <= 0 || m.RespawnMS < 0 || len(m.Capsules) > 32 {
			return nil, errors.New("invalid mount definition")
		}
		for _, capsule := range m.Capsules {
			if !nonnegative(capsule.Radius, capsule.DamageMultiplier) || capsule.Radius <= 0 {
				return nil, errors.New("invalid mount target capsule")
			}
			for _, value := range append(capsule.A[:], capsule.B[:]...) {
				if !nonnegative(float32(math.Abs(float64(value)))) {
					return nil, errors.New("invalid mount target coordinate")
				}
			}
		}
		for _, v := range append(m.Position[:], m.Rotation[:]...) {
			if !nonnegative(float32(math.Abs(float64(v)))) {
				return nil, errors.New("invalid mount placement")
			}
		}
		d.mounts = append(d.mounts, m)
	}
	if !nonnegative(source.Melee.Range, source.Melee.Damage, source.Melee.TargetRange, source.Melee.SightHeightFraction) || source.Melee.CooldownMS < 0 || source.Melee.TargetRange <= 0 || source.Melee.SightHeightFraction > 1 || math.IsNaN(float64(source.Melee.MinimumForwardDot)) || source.Melee.MinimumForwardDot < -1 || source.Melee.MinimumForwardDot > 1 {
		return nil, errors.New("invalid melee definition")
	}
	physics := source.GrenadePhysics
	if !nonnegative(-physics.Gravity, physics.BounceRetention, physics.SurfaceClearance, physics.BodyRadius, physics.StopSpeedSquared) || physics.BounceRetention > 1 {
		return nil, errors.New("invalid grenade physics")
	}
	for _, p := range source.Projectiles {
		if _, exists := d.projectiles[p.TypeID]; exists || p.TypeID < 0 || !nonnegative(p.Speed, p.VehicleDamagePercent, p.ExplosionDamage, p.ExplosionRadius, p.ContactRadius, p.RayLength, p.Damage.NearDamage, p.Damage.FarDamage, p.Damage.NearDistance, p.Damage.FarDistance) || p.ActiveMS < 0 || p.LifetimeMS < 0 || (p.Railgun && (p.ActiveMS == 0 || p.RayLength == 0)) || (p.DamagePerSecond && p.LifetimeMS == 0) || p.Damage.FarDistance < p.Damage.NearDistance || (p.ExplosionDamage > 0 && p.ExplosionRadius == 0) {
			return nil, fmt.Errorf("invalid projectile definition %d", p.TypeID)
		}
		d.projectiles[p.TypeID] = p
	}
	for _, g := range source.Grenades {
		_, duplicate := d.grenades[g.TypeID]
		_, collision := d.projectiles[g.TypeID]
		_, wireDuplicate := d.wireGrenades[g.WireIndex]
		if duplicate || collision || wireDuplicate || g.TypeID < 0 || g.TimeToLiveMS < 0 || !nonnegative(g.DamageRadius, g.Damage, g.Speed, g.Drag, g.ContactRadius, g.EffectStrength) || g.Effect < 0 || g.Effect > 2 || g.EffectDurationMS < 0 || (g.Damage > 0 && g.DamageRadius == 0) {
			return nil, fmt.Errorf("invalid grenade definition %d", g.TypeID)
		}
		d.grenades[g.TypeID] = g
		d.wireGrenades[g.WireIndex] = g.TypeID
	}
	for _, e := range source.Equipment {
		_, exists := d.equipment[e.TypeID]
		_, grenade := d.grenades[e.GrenadeTypeID]
		if exists || !grenade || e.TypeID < 1 || e.Stock < 1 || e.Stock > 255 {
			return nil, errors.New("invalid thrown equipment")
		}
		d.equipment[e.TypeID] = e
	}
	for _, w := range source.Weapons {
		if !nonnegative(w.MinimumFireIntervalMS) || w.MinimumFireIntervalMS <= 0 || w.MinimumFireIntervalMS >= math.MaxInt32 {
			return nil, errors.New("invalid weapon firing interval")
		}
		if _, _, err := w.BaseReload.schedule(1); err != nil {
			return nil, errors.New("invalid weapon reload timing")
		}
		if w.ProjectilesPerShot < 0 || w.ProjectilesPerShot > 256 || !nonnegative(w.BaseDamageMultiplier) {
			return nil, errors.New("invalid weapon projectile parameters")
		}
		if _, duplicate := d.wireWeapons[w.WireIndex]; duplicate {
			return nil, errors.New("duplicate weapon wire index")
		}
		if _, exists := d.weapons[w.TypeID]; exists || w.TypeID < 0 || w.ReserveAmmo < -1 || w.MagazineAmmo < 1 || !nonnegative(w.Range) || w.Range == 0 || len(w.FireModes) < 1 || len(w.FireModes) > 8 {
			return nil, fmt.Errorf("invalid weapon definition %d", w.TypeID)
		}
		for _, mode := range w.FireModes {
			_, projectile := d.projectiles[mode.ProjectileTypeID]
			_, grenade := d.grenades[mode.ProjectileTypeID]
			if (!projectile && !grenade) || mode.ContinuousDelayMS < 0 || mode.BaseFireDelayMS < 0 {
				return nil, fmt.Errorf("invalid fire mode in weapon %d", w.TypeID)
			}
		}
		d.weapons[w.TypeID] = w
		d.wireWeapons[w.WireIndex] = w.TypeID
	}
	for _, w := range source.MountedWeapons {
		if _, exists := d.wireWeapons[w.WireIndex]; exists || w.TypeID <= 0 || w.MagazineAmmo <= 0 || !nonnegative(w.Range, w.MinimumFireIntervalMS, w.BaseDamageMultiplier) || w.MinimumFireIntervalMS <= 0 || len(w.FireModes) < 1 || len(w.FireModes) > 2 {
			return nil, errors.New("invalid mounted weapon")
		}
		for _, mode := range w.FireModes {
			if _, ok := d.projectiles[mode.ProjectileTypeID]; !ok || mode.BaseFireDelayMS < 0 || mode.ContinuousDelayMS < 0 {
				return nil, errors.New("invalid mounted projectile")
			}
		}
		d.wireWeapons[w.WireIndex] = w.TypeID
		d.mountedWeapons[w.TypeID] = w
	}
	if err := d.loadBursts(source.BurstFire); err != nil {
		return nil, err
	}
	return d, nil
}

func hash(value string) bool { b, err := hex.DecodeString(value); return err == nil && len(b) == 32 }
func nonnegative(values ...float32) bool {
	for _, value := range values {
		if value < 0 || math.IsNaN(float64(value)) || math.IsInf(float64(value), 0) {
			return false
		}
	}
	return true
}

// Unlike encoding/json alone, this rejects duplicates, missing fields, nulls
// and case-insensitive aliases. Every numeric zero in a definition is explicit.
func shape(d *json.Decoder, t reflect.Type) error {
	optional := t.Kind() == reflect.Pointer
	if optional {
		t = t.Elem()
	}
	token, err := d.Token()
	if optional && err == nil && token == nil {
		return nil
	}
	if err != nil {
		return err
	}
	switch t.Kind() {
	case reflect.Struct:
		if token != json.Delim('{') {
			return errors.New("expected object")
		}
		fields := make(map[string]reflect.Type)
		for i := 0; i < t.NumField(); i++ {
			f := t.Field(i)
			fields[f.Tag.Get("json")] = f.Type
		}
		for d.More() {
			key, err := d.Token()
			if err != nil {
				return err
			}
			name, ok := key.(string)
			if !ok {
				return errors.New("invalid field name")
			}
			field, ok := fields[name]
			if !ok {
				return fmt.Errorf("unknown or duplicate field %q", name)
			}
			if err := shape(d, field); err != nil {
				return fmt.Errorf("%s: %w", name, err)
			}
			delete(fields, name)
		}
		if len(fields) != 0 {
			return errors.New("missing required fields")
		}
		_, err = d.Token()
	case reflect.Slice, reflect.Array:
		if token != json.Delim('[') {
			return errors.New("expected array")
		}
		count := 0
		for d.More() {
			if err := shape(d, t.Elem()); err != nil {
				return err
			}
			count++
		}
		if t.Kind() == reflect.Array && count != t.Len() {
			return errors.New("invalid fixed array length")
		}
		_, err = d.Token()
	case reflect.String:
		if _, ok := token.(string); !ok {
			return errors.New("expected string")
		}
	case reflect.Bool:
		if _, ok := token.(bool); !ok {
			return errors.New("expected boolean")
		}
	default:
		if _, ok := token.(json.Number); !ok {
			return errors.New("expected number")
		}
	}
	return err
}
