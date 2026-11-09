package authority

import "math"

func validCarOccupants(c *CarOccupants) bool {
	for _, position := range [][3]float32{c.DriverPosition, c.ShooterPivot, c.ShooterOffset} {
		for _, value := range position {
			if !nonnegative(float32(math.Abs(float64(value)))) || math.Abs(float64(value)) > 100 {
				return false
			}
		}
	}
	for _, parts := range [][]MountCapsule{c.DriverCapsules, c.ShooterCapsules} {
		if len(parts) == 0 || len(parts) > 15 {
			return false
		}
		for _, part := range parts {
			if !nonnegative(part.Radius, part.DamageMultiplier) || part.Radius <= 0 {
				return false
			}
			for _, point := range [][3]float32{part.A, part.B} {
				for _, value := range point {
					if !nonnegative(float32(math.Abs(float64(value)))) || math.Abs(float64(value)) > 100 {
						return false
					}
				}
			}
		}
	}
	return true
}
