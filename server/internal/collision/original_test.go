package collision

import (
	"encoding/json"
	"math"
	"nova3.test/backend/internal/world"
	"os"
	"testing"
)

// check_collision_runtime.py supplies derived numeric expectations in a scoped
// temporary file. Normal synthetic/Go runs do not need original game inputs.
func TestReducedCollisionOriginalFixture(t *testing.T) {
	path := os.Getenv("NOVA_COLLISION_FIXTURE")
	if path == "" {
		t.Skip("original collision parity is opt-in")
	}
	data, err := os.ReadFile("../../data/authority.json")
	if err != nil {
		t.Fatal(err)
	}
	var source struct{ Collision Model }
	if err = json.Unmarshal(data, &source); err != nil {
		t.Fatal(err)
	}
	m := &source.Collision
	if err = m.Validate(); err != nil {
		t.Fatal(err)
	}
	data, err = os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var queries []struct {
		Motion     int
		Time       float32
		Position   Vector
		Yaw, Pitch float32
		Regions    []world.HitRegion
	}
	if err = json.Unmarshal(data, &queries); err != nil {
		t.Fatal(err)
	}
	for index, q := range queries {
		actual := m.regions(m.motion(q.Motion, q.Time), q.Position, q.Yaw, q.Pitch)
		if len(actual) != len(q.Regions) {
			t.Fatal("region count", index)
		}
		for i, a := range actual {
			b := q.Regions[i]
			if a.PartID != b.PartID || a.DamageMultiplier != b.DamageMultiplier {
				t.Fatal("region identity", index, i)
			}
			left := []float64{a.Volume.A.X, a.Volume.A.Y, a.Volume.A.Z, a.Volume.B.X, a.Volume.B.Y, a.Volume.B.Z, a.Volume.Radius}
			right := []float64{b.Volume.A.X, b.Volume.A.Y, b.Volume.A.Z, b.Volume.B.X, b.Volume.B.Y, b.Volume.B.Z, b.Volume.Radius}
			for j := range left {
				if math.Abs(left[j]-right[j]) > .00005 {
					t.Fatalf("pose%d region%d axis%d: got%.9f expected%.9f", index, i, j, left[j], right[j])
				}
			}
		}
	}
	t.Logf("%d original-derived posed capsule sets match", len(queries))
}
