package world

import "math"

// RayCapsule intersects a finite segment with a capsule, including its end
// spheres. Initial overlap is a hit at zero, as with CapsuleCast. No direction
// normalization or world-up assumption is required.
func RayCapsule(origin, displacement Vec3, capsule Capsule) (Hit, bool, error) {
	if !origin.valid() || !displacement.valid() || !capsule.A.valid() || !capsule.B.valid() || !valid(capsule.Radius) || capsule.Radius <= 0 {
		return Hit{}, false, ErrGeometry
	}
	axis := capsule.B.Sub(capsule.A)
	axis2 := axis.Dot(axis)
	closest := func(point Vec3) Vec3 {
		if axis2 == 0 {
			return capsule.A
		}
		return capsule.A.Add(axis.Mul(clamp(point.Sub(capsule.A).Dot(axis)/axis2, 0, 1)))
	}
	normal := origin.Sub(closest(origin))
	if normal.Dot(normal) <= capsule.Radius*capsule.Radius {
		return Hit{Point: origin, Normal: normal.unit()}, true, nil
	}
	speed2 := displacement.Dot(displacement)
	if speed2 == 0 {
		return Hit{}, false, nil
	}
	best := math.Inf(1)
	// Roots are bounded to the actual ray segment. A stable quadratic avoids
	// cancellation when a long ray begins very near the capsule surface.
	roots := func(a, b, c float64, accept func(float64) bool) {
		if a == 0 {
			return
		}
		discriminant := b*b - a*c
		if discriminant < 0 {
			return
		}
		q := -b - math.Copysign(math.Sqrt(discriminant), b)
		values := [2]float64{q / a, math.Inf(1)}
		if q != 0 {
			values[1] = c / q
		}
		for _, fraction := range values {
			if fraction >= 0 && fraction <= 1 && fraction < best && accept(fraction) {
				best = fraction
			}
		}
	}
	for _, center := range []Vec3{capsule.A, capsule.B} {
		relative := origin.Sub(center)
		roots(speed2, relative.Dot(displacement), relative.Dot(relative)-capsule.Radius*capsule.Radius, func(float64) bool { return true })
	}
	if axis2 > 0 {
		relative := origin.Sub(capsule.A)
		along := displacement.Dot(axis) / axis2
		start := relative.Dot(axis) / axis2
		perpendicular := displacement.Sub(axis.Mul(along))
		offset := relative.Sub(axis.Mul(start))
		roots(perpendicular.Dot(perpendicular), offset.Dot(perpendicular), offset.Dot(offset)-capsule.Radius*capsule.Radius, func(f float64) bool {
			position := start + f*along
			return position >= 0 && position <= 1
		})
	}
	if math.IsInf(best, 1) {
		return Hit{}, false, nil
	}
	point := origin.Add(displacement.Mul(best))
	return Hit{Fraction: best, Point: point, Normal: point.Sub(closest(point)).unit()}, true, nil
}
