package world

import (
	"math"
	"testing"
)

func TestPlacementRotationScaleTranslation(t *testing.T) {
	source := []Surface{{A: Vec3{1, 0, 0}, B: Vec3{0, 1, 0}, C: Vec3{0, 0, 1}, Object: 91, Material: 19, Mask: Projectile}}
	tr := Transform{Position: Vec3{10, 20, 30}, Rotation: Vec3{0, 0, 90}, Scale: Vec3{2, 3, -4}}
	placed, err := tr.Place(source)
	if err != nil {
		t.Fatal(err)
	}
	for i, p := range []Vec3{placed[0].A, placed[0].B, placed[0].C} {
		expected := []Vec3{{10, 22, 30}, {7, 20, 30}, {10, 20, 26}}[i]
		near(t, p.Sub(expected).length(), 0, 1e-12)
	}
	if placed[0].Object != 91 || placed[0].Material != 19 || placed[0].Mask != Projectile || source[0].A != (Vec3{1, 0, 0}) {
		t.Fatal("metadata or input changed")
	}
	// Mixed-angle golden vector agrees with the original matrix convention
	// checked in native-world-reader-tests, and catches reversed rotation order.
	tr.Rotation = Vec3{90, 90, 90}
	tr.Scale = Vec3{1, 1, 1}
	tr.Position = Vec3{}
	placed, err = tr.Place(source)
	if err != nil {
		t.Fatal(err)
	}
	for i, p := range []Vec3{placed[0].A, placed[0].B, placed[0].C} {
		near(t, p.Sub([]Vec3{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}[i]).length(), 0, 1e-12)
	}
}

func TestInvalidPlacementFailsAtomically(t *testing.T) {
	s := []Surface{floor(0, 1)}
	for _, tr := range []Transform{
		{Position: Vec3{math.NaN(), 0, 0}},
		{Rotation: Vec3{0, math.Inf(1), 0}},
		{Scale: Vec3{1e9, 1e9, 1e9}},
	} {
		if result, err := tr.Place(s); err == nil || result != nil {
			t.Fatal("bad transform published", tr)
		}
	}
	tr := Transform{Scale: Vec3{0, 0, 0}}
	placed, err := tr.Place(s)
	if err != nil {
		t.Fatal(err)
	}
	m := mesh(t, placed...)
	if len(m.triangles) != 0 {
		t.Fatal("collapsed surface retained")
	}
}
