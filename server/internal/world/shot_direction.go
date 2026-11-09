package world

import "math"

// DecodeShotDirection reverses the original normalized component encoding:
// trunc((component+1)*32767.5). Reject vectors outside its quantization error.
func DecodeShotDirection(v [3]uint16) (Vec3, error) {
	decode := func(x uint16) float64 { return float64(float32(float32(x)*float32(2./65535)) - 1) }
	d := Vec3{decode(v[0]), decode(v[1]), decode(v[2])}
	n := math.Sqrt(d.Dot(d))
	if math.Abs(n-1) > math.Sqrt(3)*2/65535 {
		return Vec3{}, ErrGeometry
	}
	return d.Mul(1 / n), nil
}
