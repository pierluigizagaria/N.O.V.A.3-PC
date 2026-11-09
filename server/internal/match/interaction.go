package match

import "nova3.test/backend/internal/world"

// Player.CheckInteract compares the full root-to-height player capsule against
// authored interactive capsules. These are interaction volumes, not hitboxes.
func (s *Session) canReachMount(p *peer, v *mountState) bool {
	pose := s.movement.poses[p.player.Slot].Position
	root := world.Vec3{X: float64(pose[0]), Y: float64(pose[1]), Z: float64(pose[2])}
	body := s.authority.Player().Body
	top := root.Add(world.Vec3{Z: float64(body.Height + body.OffsetZ)})
	player := world.Capsule{A: root, B: top, Radius: float64(body.Radius)}
	var regions []world.HitRegion
	if v.mech != nil {
		regions = v.mech.PlaceRegions(v.definition.Interaction)
	} else {
		frame := frameEuler(v.position, v.rotation)
		for _, part := range v.definition.Interaction {
			regions = append(regions, world.HitRegion{Volume: world.Capsule{
				A: frame.point(part.A), B: frame.point(part.B), Radius: float64(part.Radius)}})
		}
	}
	for _, region := range regions {
		if _, hit, err := world.CapsuleContact(player, region.Volume); err != nil || !hit {
			continue
		}
		if v.definition.Category != 3 {
			end := v.position.Add(world.Vec3{Z: float64(body.Height + body.OffsetZ)})
			if _, blocked, err := s.movement.scene.Raycast(top, end, world.Filter{Group: 0x7ef7, Mask: 6}, nil); err != nil || blocked {
				return false
			}
		}
		return true
	}
	return false
}
