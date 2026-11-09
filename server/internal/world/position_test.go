package world

import (
	"math"
	"testing"
)

func TestNativePositionQuantization(t *testing.T) {
	bounds := PositionBounds{Vec3{-20, 10, -100}, Vec3{80, 110, 100}}
	for _, tc := range []struct {
		wire [3]uint16
		want Vec3
	}{
		{[3]uint16{}, bounds.Minimum},
		{[3]uint16{65535, 65535, 65535}, bounds.Maximum},
		{[3]uint16{13107, 26214, 39321}, Vec3{0, 50, 20}},
	} {
		got, err := bounds.DecodePosition(tc.wire)
		if err != nil || got != tc.want {
			t.Fatalf("%v: %v, %v", tc.wire, got, err)
		}
	}
	// Quantization asymmetry around zero is retained, not snapped to a midpoint.
	center, err := (PositionBounds{Vec3{-1, -1, -1}, Vec3{1, 1, 1}}).DecodePosition([3]uint16{32767, 32768, 32767})
	if err != nil || center.X >= 0 || center.Y <= 0 {
		t.Fatal(center, err)
	}
	for _, bad := range []PositionBounds{
		{}, {Vec3{1, 0, 0}, Vec3{0, 1, 1}},
		{Vec3{math.NaN(), 0, 0}, Vec3{1, 1, 1}},
		{Vec3{0, 0, 0}, Vec3{1, math.Inf(1), 1}},
	} {
		if _, err := bad.DecodePosition([3]uint16{}); err == nil {
			t.Fatal("accepted invalid bounds", bad)
		}
	}
}
