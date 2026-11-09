package authority

// Original consumable identities are protocol enums. Only reduced numeric
// parameters are distributed; runtime needs no original resources.
type Consumables struct {
	CooldownMS           int32   `json:"cooldown_ms"`
	XPMultiplier         float32 `json:"xp_multiplier"`
	CriticalChance       float32 `json:"critical_chance"`
	CriticalMultiplier   float32 `json:"critical_multiplier"`
	SpeedBoost           float32 `json:"speed_boost"`
	PrecisionMS          int32   `json:"precision_ms"`
	AgilityMS            int32   `json:"agility_ms"`
	PerceptionMS         int32   `json:"perception_ms"`
	CamouflageMS         int32   `json:"camouflage_ms"`
	CountertechMS        int32   `json:"countertech_ms"`
	OverchargeMS         int32   `json:"overcharge_ms"`
	OverchargeHealthCost float32 `json:"overcharge_health_cost"`
}

func (c Consumables) valid() bool {
	if !nonnegative(c.XPMultiplier, c.CriticalChance, c.CriticalMultiplier, c.SpeedBoost, c.OverchargeHealthCost) || c.XPMultiplier < 1 || c.CriticalChance > 1 || c.CriticalMultiplier < 1 || c.OverchargeHealthCost > 10000 {
		return false
	}
	for _, ms := range []int32{c.CooldownMS, c.PrecisionMS, c.AgilityMS, c.PerceptionMS, c.CamouflageMS, c.CountertechMS, c.OverchargeMS} {
		if ms <= 0 || ms > 3600000 {
			return false
		}
	}
	return true
}

func (d *Definitions) Consumables() Consumables { return d.consumables }
