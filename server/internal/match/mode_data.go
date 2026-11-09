package match

import (
	"bytes"
	_ "embed"
	"encoding/json"
	"fmt"
	"math"
	"sync"

	"nova3.test/backend/internal/world"
)

// This versioned numeric resource contains no original executable, scripts,
// resource names, art or full game files. Runtime needs no original inputs.
//
//go:embed mode_data.json
var modeData []byte

type objectiveDefinition struct {
	Object    uint32     `json:"object"`
	Zone      int32      `json:"zone"`
	Position  [3]float64 `json:"position"`
	Enabled   bool       `json:"enabled"`
	Radius    float64    `json:"radius"`
	Team      byte       `json:"team"`
	Rate      float64    `json:"rate"`
	Threshold float64    `json:"threshold"`
}

func (d objectiveDefinition) position() world.Vec3 {
	return world.Vec3{X: d.Position[0], Y: d.Position[1], Z: d.Position[2]}
}

type objectiveMap struct {
	Map    byte                  `json:"map"`
	Flags  []objectiveDefinition `json:"flags"`
	Bases  []objectiveDefinition `json:"bases"`
	Points []objectiveDefinition `json:"points"`
}
type objectiveRules struct {
	OvertimeMinutes         int     `json:"overtime_minutes"`
	PointReadyMS            int     `json:"point_ready_ms"`
	SpawnTeammateClearance  float64 `json:"spawn_teammate_clearance"`
	SpawnObjectiveClearance float64 `json:"spawn_objective_enemy_clearance"`
	SpawnFallbackCount      int     `json:"spawn_fallback_count"`
	RescueDistanceSquared   float64 `json:"freeze_rescue_distance_squared"`
	RescueMS                int     `json:"freeze_rescue_ms"`
	FreezeRoundMS           int     `json:"freeze_round_settle_ms"`
	PointHoldMS             int     `json:"point_hold_ms"`
	PointMaximum            float64 `json:"point_maximum"`
	PointScale              float64 `json:"point_snapshot_scale"`
	FlagHomeTolerance       float64 `json:"flag_home_tolerance"`
	FlagReturnTolerance     float64 `json:"flag_return_tolerance"`
}
type objectiveDocument struct {
	Version int               `json:"schema_version"`
	Library string            `json:"library_sha256"`
	World   string            `json:"world_sha256"`
	Rules   objectiveRules    `json:"rules"`
	Rewards map[string]uint32 `json:"rewards"`
	Maps    []objectiveMap    `json:"maps"`
}

var loadModeData = sync.OnceValues(func() (*objectiveDocument, error) {
	var v objectiveDocument
	d := json.NewDecoder(bytes.NewReader(modeData))
	d.DisallowUnknownFields()
	if err := d.Decode(&v); err != nil {
		return nil, err
	}
	if v.Version != 1 || len(v.Library) != 64 || len(v.World) != 64 || len(v.Maps) != 6 || v.Rules.PointMaximum <= 0 ||
		v.Rules.OvertimeMinutes <= 0 || v.Rules.PointReadyMS < 0 ||
		v.Rules.SpawnFallbackCount <= 0 || v.Rules.SpawnTeammateClearance <= 0 || v.Rules.SpawnObjectiveClearance <= 0 ||
		v.Rules.PointScale <= 0 || v.Rules.RescueDistanceSquared <= 0 || v.Rules.RescueMS <= 0 || v.Rules.FreezeRoundMS <= 0 || v.Rules.PointHoldMS <= 0 {
		return nil, fmt.Errorf("invalid numeric objective rules")
	}
	for i, m := range v.Maps {
		if int(m.Map) != i || len(m.Points) > 32 {
			return nil, ErrState
		}
		ids := map[uint32]bool{}
		for _, list := range [][]objectiveDefinition{m.Flags, m.Bases, m.Points} {
			for _, obj := range list {
				if obj.Object == 0 || obj.Zone < 0 || ids[obj.Object] || obj.Team > 2 || obj.Radius <= 0 || math.IsNaN(obj.Radius) || math.IsInf(obj.Radius, 0) {
					return nil, ErrState
				}
				for _, x := range obj.Position {
					if math.IsNaN(x) || math.IsInf(x, 0) {
						return nil, ErrState
					}
				}
				ids[obj.Object] = true
			}
		}
		for _, p := range m.Points {
			if p.Rate <= 0 || p.Threshold <= 0 || p.Threshold >= v.Rules.PointMaximum/2 {
				return nil, ErrState
			}
		}
	}
	for _, key := range []string{"flag_pick", "flag_return", "flag_capture", "point_capture", "point_enemy_capture", "point_score", "freeze_first", "freeze_streak", "unfreeze_first", "unfreeze_streak"} {
		if value, ok := v.Rewards[key]; !ok || value > 1000000 {
			return nil, ErrState
		}
	}
	return &v, nil
})

func CheckModeData() error { _, err := loadModeData(); return err }
