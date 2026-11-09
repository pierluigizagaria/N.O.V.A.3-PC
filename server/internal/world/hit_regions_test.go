package world

import (
	"math"
	"testing"
)

func TestContactRegionsOriginalCompetitiveOrdering(t *testing.T) {
	shot := Capsule{Vec3{}, Vec3{10, 0, 0}, 0}
	regions := []HitRegion{
		{1, Capsule{Vec3{5, -.5, 0}, Vec3{5, .5, 0}, .22}, 1},
		{3, Capsule{Vec3{6, -.5, 0}, Vec3{6, .5, 0}, .22}, 3},
	}
	contact, hit, err := ContactRegions(shot, regions)
	if err != nil || !hit || contact != (RegionContact{1, Vec3{5, 0, 0}, 1}) {
		t.Fatalf("native body-first contact: %+v %v %v", contact, hit, err)
	}
	// Native query is authored order, not minimum distance along the shot.
	regions[0], regions[1] = regions[1], regions[0]
	contact, hit, err = ContactRegions(shot, regions)
	if err != nil || !hit || contact != (RegionContact{3, Vec3{6, 0, 0}, 3}) {
		t.Fatalf("native first region: %+v %v %v", contact, hit, err)
	}
	regions[0], regions[1] = regions[1], regions[0]
	regions[0].Volume.A.Z, regions[0].Volume.B.Z = 5, 5
	contact, hit, err = ContactRegions(shot, regions)
	if err != nil || !hit || contact.PartID != 3 || contact.DamageMultiplier != 3 {
		t.Fatalf("native exposed head: %+v %v %v", contact, hit, err)
	}
	shot.B.X = 4
	if _, hit, err := ContactRegions(shot, regions); err != nil || hit {
		t.Fatalf("hit beyond finite segment: %v %v", hit, err)
	}
}

func TestContactRegionsRejectsInvalidPoseAfterEarlierHit(t *testing.T) {
	validRegion := HitRegion{1, Capsule{Vec3{}, Vec3{0, 1, 0}, 1}, 1}
	for _, bad := range []HitRegion{
		validRegion,
		{0, validRegion.Volume, 1},
		{2, validRegion.Volume, -1},
		{2, validRegion.Volume, float32(math.NaN())},
		{2, Capsule{Radius: -1}, 1},
	} {
		if result, hit, err := ContactRegions(Capsule{}, []HitRegion{validRegion, bad}); err == nil || hit || result != (RegionContact{}) {
			t.Fatalf("invalid pose granted contact: %+v %v %v", result, hit, err)
		}
	}
	if _, hit, err := ContactRegions(Capsule{}, nil); err != nil || hit {
		t.Fatalf("missing pose should grant no contact: %v %v", hit, err)
	}
}
