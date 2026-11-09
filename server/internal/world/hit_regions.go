package world

// HitRegion is one already-posed, query-ready actor collider. Radius includes
// the authority definition's query inflation. Callers must supply faithful
// animated poses; the movement capsule is not a substitute for this geometry.
type HitRegion struct {
	PartID           uint16
	Volume           Capsule
	DamageMultiplier float32
}

type RegionContact struct {
	PartID           uint16
	Point            Vec3
	DamageMultiplier float32
}

// ContactRegions preserves the original competitive-player impact ordering.
// The first overlapping authored body wins, even if another region is nearer
// or has a higher aiming priority. Aiming uses a different native query.
func ContactRegions(shot Capsule, regions []HitRegion) (RegionContact, bool, error) {
	var result RegionContact
	found := false
	seen := make(map[uint16]bool, len(regions))
	for _, region := range regions {
		if region.PartID == 0 || seen[region.PartID] || !valid(float64(region.DamageMultiplier)) || region.DamageMultiplier < 0 {
			return RegionContact{}, false, ErrGeometry
		}
		seen[region.PartID] = true
		point, contact, err := CapsuleContact(shot, region.Volume)
		if err != nil {
			return RegionContact{}, false, err
		}
		// Validate the whole supplied pose before granting any impact.
		if contact && !found {
			result = RegionContact{region.PartID, point, region.DamageMultiplier}
			found = true
		}
	}
	return result, found, nil
}
