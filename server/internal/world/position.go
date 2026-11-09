package world

// PositionBounds is the native mesh-AABB union used by the original game to
// quantize positions. It includes invisible broadphase-disabled mesh objects;
// the gameplay collision Scene intentionally has a different query policy.
type PositionBounds struct{ Minimum, Maximum Vec3 }

// EncodePosition uses the native float32 scale and truncation. Server-owned
// objects outside the quantization domain cannot be represented by this packet.
func (b PositionBounds) EncodePosition(v Vec3) ([3]uint16, error) {
	var out [3]uint16
	if !b.valid() || !v.valid() {
		return out, ErrGeometry
	}
	for i := range out {
		lo, hi, x := float32(b.Minimum.axis(i)), float32(b.Maximum.axis(i)), float32(v.axis(i))
		if x < lo || x > hi {
			return out, ErrGeometry
		}
		scale := float32(65535) / float32(hi-lo)
		out[i] = uint16(min(float32(65535), float32(float32(x-lo)*scale)))
	}
	return out, nil
}

func (b PositionBounds) valid() bool {
	return b.Minimum.valid() && b.Maximum.valid() && b.Minimum.X < b.Maximum.X &&
		b.Minimum.Y < b.Maximum.Y && b.Minimum.Z < b.Maximum.Z
}

// DecodePosition preserves the original float32 domain and rounding. This is
// decoding only: a shot origin still needs authenticated actor/pose validation.
func (b PositionBounds) DecodePosition(v [3]uint16) (Vec3, error) {
	if !b.valid() {
		return Vec3{}, ErrGeometry
	}
	var coordinates [3]float64
	for i := range coordinates {
		lo, hi := float32(b.Minimum.axis(i)), float32(b.Maximum.axis(i))
		step := float32((hi - lo) * float32(1.0/65535.0))
		coordinates[i] = float64(float32(float32(v[i])*step) + lo)
	}
	return Vec3{coordinates[0], coordinates[1], coordinates[2]}, nil
}
