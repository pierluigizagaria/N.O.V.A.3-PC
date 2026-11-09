package authority

import "nova3.test/backend/internal/collision"

func validMountInteraction(m Mount, mech *collision.MechModel) bool {
	if len(m.Interaction) == 0 || len(m.Interaction) > 16 {
		return false
	}
	for _, part := range m.Interaction {
		if m.Category == 2 && part.Frame < 0 {
			return false
		}
		if !nonnegative(part.Radius) || part.Radius <= 0 || part.Radius > 100 || part.Frame < -1 {
			return false
		}
		if part.Frame >= 0 && (m.Category != 2 || mech == nil || part.Frame >= len(mech.Frames)) {
			return false
		}
		for _, point := range [][3]float32{part.A, part.B} {
			for _, value := range point {
				if value < -100 || value > 100 || value != value {
					return false
				}
			}
		}
	}
	return true
}
