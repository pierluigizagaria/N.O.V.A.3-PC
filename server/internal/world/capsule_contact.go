package world

import "math"

// CapsuleContact implements the original gameplay capsule/capsule query. Point
// lies on the first capsule's axis at the closest approach, not at the surface
// entry returned by RayCapsule. A zero radius represents a finite shot segment.
// Original native probes cover tangency, radius addition and endpoint contact.
func CapsuleContact(shot, target Capsule) (point Vec3, contact bool, err error) {
	for _, c := range []Capsule{shot, target} {
		if !c.A.valid() || !c.B.valid() || !valid(c.Radius) || c.Radius < 0 || c.Radius > math.MaxFloat32 {
			return Vec3{}, false, ErrGeometry
		}
		for _, v := range []float64{c.A.X, c.A.Y, c.A.Z, c.B.X, c.B.Y, c.B.Z} {
			if math.Abs(v) > math.MaxFloat32 {
				return Vec3{}, false, ErrGeometry
			}
		}
	}
	// Preserve the original float32 arithmetic and squared-length degeneracy
	// threshold. Reusing the double-precision movement helper changes short-axis
	// hit decisions. This is an algorithm tolerance, not a body dimension.
	type vector [3]float32
	vec := func(v Vec3) vector { return vector{float32(v.X), float32(v.Y), float32(v.Z)} }
	sub := func(a, b vector) vector { return vector{a[0] - b[0], a[1] - b[1], a[2] - b[2]} }
	dot := func(a, b vector) float32 { return float32(float32(a[0]*b[0])+float32(a[1]*b[1])) + float32(a[2]*b[2]) }
	bound := func(v float32) float32 { return min(max(v, 0), 1) }
	a, b, c, d := vec(shot.A), vec(shot.B), vec(target.A), vec(target.B)
	u, v, w := sub(b, a), sub(d, c), sub(a, c)
	aa, bb, cc, dd, ee := dot(u, u), dot(u, v), dot(v, v), dot(u, w), dot(v, w)
	for _, value := range []float32{aa, bb, cc, dd, ee} {
		if !valid(float64(value)) {
			return Vec3{}, false, ErrGeometry
		}
	}
	const epsilon = float32(.001)
	s, t := float32(0), float32(0)
	if aa <= epsilon {
		if cc > epsilon {
			t = bound(ee / cc)
		}
	} else if cc <= epsilon {
		s = bound(-dd / aa)
	} else {
		denominator := float32(aa*cc) - float32(bb*bb)
		if denominator != 0 {
			s = bound((float32(bb*ee) - float32(cc*dd)) / denominator)
		}
		numerator := float32(bb*s) + ee
		if numerator < 0 {
			s = bound(-dd / aa)
		} else if numerator > cc {
			t = 1
			s = bound((bb - dd) / aa)
		} else {
			t = numerator / cc
		}
	}
	var p, q vector
	for i := range p {
		p[i], q[i] = a[i]+float32(u[i]*s), c[i]+float32(v[i]*t)
	}
	delta := sub(p, q)
	radius := float32(shot.Radius) + float32(target.Radius)
	distance2, radius2 := dot(delta, delta), float32(radius*radius)
	point = Vec3{float64(p[0]), float64(p[1]), float64(p[2])}
	if !point.valid() || !valid(float64(distance2)) || !valid(float64(radius2)) {
		return Vec3{}, false, ErrGeometry
	}
	return point, distance2 <= radius2, nil
}
