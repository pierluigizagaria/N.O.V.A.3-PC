package world

import (
	"math"
	"testing"
)

func TestCapsuleContactOriginalCases(t *testing.T) {
	for _, tc := range []struct {
		name         string
		shot, target Capsule
		point        Vec3
		hit          bool
	}{
		{"closest point is not surface entry", Capsule{Vec3{}, Vec3{10, 0, 0}, 0}, Capsule{Vec3{5, -1, 0}, Vec3{5, 1, 0}, 1}, Vec3{5, 0, 0}, true},
		{"tangent", Capsule{Vec3{}, Vec3{10, 0, 0}, 0}, Capsule{Vec3{5, -1, 1}, Vec3{5, 1, 1}, 1}, Vec3{5, 0, 0}, true},
		{"miss", Capsule{Vec3{}, Vec3{10, 0, 0}, 0}, Capsule{Vec3{5, -1, 1.01}, Vec3{5, 1, 1.01}, 1}, Vec3{5, 0, 0}, false},
		{"swept radius", Capsule{Vec3{}, Vec3{10, 0, 0}, .02}, Capsule{Vec3{5, -1, 1.01}, Vec3{5, 1, 1.01}, 1}, Vec3{5, 0, 0}, true},
		{"beyond segment", Capsule{Vec3{}, Vec3{10, 0, 0}, 0}, Capsule{Vec3{12, 0, 0}, Vec3{12, 0, 0}, 1}, Vec3{10, 0, 0}, false},
		{"sphere endpoint", Capsule{Vec3{}, Vec3{10, 0, 0}, 0}, Capsule{Vec3{10.5, 0, 0}, Vec3{10.5, 0, 0}, 1}, Vec3{10, 0, 0}, true},
		{"short axes are points", Capsule{Vec3{}, Vec3{.02, 0, 0}, 0}, Capsule{Vec3{.02, 0, 0}, Vec3{.02, 0, .02}, .01}, Vec3{}, false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			p, hit, err := CapsuleContact(tc.shot, tc.target)
			if err != nil || hit != tc.hit || p != tc.point {
				t.Fatalf("point=%v hit=%v err=%v", p, hit, err)
			}
		})
	}
}

func TestCapsuleContactRejectsInvalidAndOverflow(t *testing.T) {
	for _, c := range []Capsule{
		{Radius: -1}, {Radius: math.Inf(1)}, {A: Vec3{math.NaN(), 0, 0}},
		{B: Vec3{math.MaxFloat32, 0, 0}}, {Radius: math.MaxFloat32},
	} {
		if _, _, err := CapsuleContact(c, Capsule{}); err == nil {
			t.Fatalf("accepted %v", c)
		}
	}
}
