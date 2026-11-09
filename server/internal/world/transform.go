package world

import "math"

// Transform is an object's placement, not a render-node transform. Rotation
// follows the original CMatrix4::setRotationDegrees convention (Rz * Ry * Rx).
// Collision vertices are scaled before the rigid body rotation/translation.
type Transform struct {
	Position Vec3
	Rotation Vec3
	Scale    Vec3
}

func (t Transform) valid() bool {
	return t.Position.valid() && t.Rotation.valid() && t.Scale.valid()
}

func (t Transform) columns() [3]Vec3 {
	const radians = math.Pi / 180
	sx, cx := math.Sincos(t.Rotation.X * radians)
	sy, cy := math.Sincos(t.Rotation.Y * radians)
	sz, cz := math.Sincos(t.Rotation.Z * radians)
	return [3]Vec3{
		{cy * cz, cy * sz, -sy},
		{sx*sy*cz - cx*sz, sx*sy*sz + cx*cz, sx * cy},
		{cx*sy*cz + sx*sz, cx*sy*sz - sx*cz, cx * cy},
	}
}

// Place preserves surface IDs/materials/query masks. The input is never
// modified. Zero or negative scale is meaningful; degenerate placed faces are
// omitted by NewMesh. Reject numeric overflow before publishing any geometry.
func (t Transform) Place(local []Surface) ([]Surface, error) {
	if !t.valid() {
		return nil, ErrGeometry
	}
	columns := t.columns()
	point := func(p Vec3) (Vec3, error) {
		if !p.valid() {
			return Vec3{}, ErrGeometry
		}
		v := t.Position.Add(columns[0].Mul(p.X * t.Scale.X)).Add(columns[1].Mul(p.Y * t.Scale.Y)).Add(columns[2].Mul(p.Z * t.Scale.Z))
		if !v.valid() {
			return Vec3{}, ErrGeometry
		}
		return v, nil
	}
	result := make([]Surface, len(local))
	for i, s := range local {
		var err error
		if s.Mask & ^All != 0 {
			return nil, ErrGeometry
		}
		if s.A, err = point(s.A); err != nil {
			return nil, err
		}
		if s.B, err = point(s.B); err != nil {
			return nil, err
		}
		if s.C, err = point(s.C); err != nil {
			return nil, err
		}
		result[i] = s
	}
	return result, nil
}
