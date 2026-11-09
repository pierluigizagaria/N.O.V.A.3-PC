package match

import (
	"encoding/binary"
	"math"

	"nova3.test/backend/internal/authority"
	"nova3.test/backend/internal/world"
)

// A car proxy follows its actual attachment, not the hidden marine actor.
// The driver's root takes the car rotation. The shooter's root takes the
// shooter attachment beneath the platform's received local rotation (native147).
type mountFrame struct {
	position world.Vec3
	axes     [3]world.Vec3
}

func frameEuler(position world.Vec3, angles [3]float64) mountFrame {
	x, y, z := angles[0]*math.Pi/180, angles[1]*math.Pi/180, angles[2]*math.Pi/180
	sx, cx := math.Sincos(x)
	sy, cy := math.Sincos(y)
	sz, cz := math.Sincos(z)
	return mountFrame{position: position, axes: [3]world.Vec3{
		{X: cz * cy, Y: sz * cy, Z: -sy},
		{X: sy*sx*cz - cx*sz, Y: sy*sx*sz + cx*cz, Z: cy * sx},
		{X: sy*cx*cz + sz*sx, Y: sy*cx*sz - cz*sx, Z: cy * cx},
	}}
}
func (f mountFrame) direction(p world.Vec3) world.Vec3 {
	return f.axes[0].Mul(p.X).Add(f.axes[1].Mul(p.Y)).Add(f.axes[2].Mul(p.Z))
}
func (f mountFrame) point(p [3]float32) world.Vec3 {
	return f.position.Add(f.direction(world.Vec3{X: float64(p[0]), Y: float64(p[1]), Z: float64(p[2])}))
}
func (v *mountState) occupantFrame(seat byte) mountFrame {
	f := frameEuler(v.position, v.rotation)
	c := v.definition.Occupants
	if seat == 0 {
		f.position = f.point(c.DriverPosition)
		return f
	}
	position := f.point(c.ShooterPivot)
	var rotation [3]float64
	if len(v.shooter) == 14 {
		for axis := range rotation {
			rotation[axis] = float64(binary.BigEndian.Uint16(v.shooter[2+2*axis:])) * .01
		}
	}
	local := frameEuler(world.Vec3{}, rotation)
	for axis := range local.axes {
		local.axes[axis] = f.direction(local.axes[axis])
	}
	local.position = position
	local.position = local.point(c.ShooterOffset)
	return local
}
func (v *mountState) occupantRegions(seat byte) []world.HitRegion {
	c := v.definition.Occupants
	if v.definition.Category != 1 || c == nil || v.health <= 0 || seat > 1 {
		return nil
	}
	parts := c.DriverCapsules
	if seat == 1 {
		parts = c.ShooterCapsules
	}
	return placeMountCapsules(v.occupantFrame(seat), parts)
}
func placeMountCapsules(frame mountFrame, parts []authority.MountCapsule) []world.HitRegion {
	result := make([]world.HitRegion, len(parts))
	for i, part := range parts {
		result[i] = world.HitRegion{PartID: uint16(i + 1), DamageMultiplier: part.DamageMultiplier,
			Volume: world.Capsule{A: frame.point(part.A), B: frame.point(part.B), Radius: float64(part.Radius)}}
	}
	return result
}
