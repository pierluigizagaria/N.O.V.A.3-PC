package world

import (
	"math"
	"testing"
)

func TestNativeShotDirection(t *testing.T) {
	for _, v := range []Vec3{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, 0, -1}, {1, 2, 3}, {-.7, .3, -.2}} {
		v = v.Mul(1 / math.Sqrt(v.Dot(v)))
		var encoded [3]uint16
		for i, c := range []float64{v.X, v.Y, v.Z} {
			encoded[i] = uint16(float32(float32(c)+1) * 32767.5)
		}
		got, err := DecodeShotDirection(encoded)
		if err != nil || got.Sub(v).length() > .00006 {
			t.Fatal(v, got, err)
		}
	}
	for _, v := range [][3]uint16{{0, 0, 0}, {65535, 65535, 65535}, {32767, 32767, 32767}, {40000, 40000, 40000}} {
		if _, err := DecodeShotDirection(v); err == nil {
			t.Fatal("invalid direction", v)
		}
	}
}
