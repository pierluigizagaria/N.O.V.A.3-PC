package world

import (
	"math"
	"testing"
)

func TestRayCapsuleBodyCapsAndSegmentLimits(t *testing.T) {
	c := Capsule{Vec3{0, 0, 1}, Vec3{0, 0, 3}, 1}
	for _, tc := range []struct {
		from, delta Vec3
		hit         bool
		fraction    float64
	}{
		{Vec3{-3, 0, 2}, Vec3{6, 0, 0}, true, 1.0 / 3},
		{Vec3{0, 0, 6}, Vec3{0, 0, -6}, true, 1.0 / 3},
		{Vec3{0, 0, -2}, Vec3{0, 0, 6}, true, 1.0 / 3},
		{Vec3{-3, 1, 2}, Vec3{6, 0, 0}, true, .5},
		{Vec3{-3, 1.001, 2}, Vec3{6, 0, 0}, false, 0},
		{Vec3{-3, 0, 2}, Vec3{1, 0, 0}, false, 0},
		{Vec3{-3, 0, 2}, Vec3{-6, 0, 0}, false, 0},
		{Vec3{0, 0, 2}, Vec3{}, true, 0},
		{Vec3{2, 0, 2}, Vec3{}, false, 0},
	} {
		for _, capsule := range []Capsule{c, {c.B, c.A, c.Radius}} {
			h, hit, err := RayCapsule(tc.from, tc.delta, capsule)
			if err != nil || hit != tc.hit || (hit && math.Abs(h.Fraction-tc.fraction) > 1e-9) {
				t.Fatal(tc, h, hit, err)
			}
		}
	}
}

func TestRayCapsuleSphereRotationTranslationAndTinyAxis(t *testing.T) {
	for _, axis := range []Vec3{{}, {0, 0, 1e-12}, {0, 0, 4}, {4, 0, 0}, {0, 4, 0}} {
		center := Vec3{43, -20, 7}
		c := Capsule{center.Sub(axis.Mul(.5)), center.Add(axis.Mul(.5)), .5}
		direction := Vec3{0, 1, 0}
		if axis.Y != 0 {
			direction = Vec3{1, 0, 0}
		}
		h, ok, err := RayCapsule(center.Sub(direction.Mul(10)), direction.Mul(20), c)
		if err != nil || !ok || math.Abs(h.Fraction-.475) > 1e-9 {
			t.Fatal(axis, h, ok, err)
		}
	}
	for _, invalid := range []float64{0, -1, math.NaN(), math.Inf(1)} {
		if _, _, err := RayCapsule(Vec3{}, Vec3{1, 0, 0}, Capsule{Radius: invalid}); err == nil {
			t.Fatal("invalid radius accepted")
		}
	}
}
