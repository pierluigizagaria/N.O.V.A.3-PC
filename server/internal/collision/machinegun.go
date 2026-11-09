package collision

import (
	"math"
	"nova3.test/backend/internal/world"
)

func (a *Actor) SetMachinegun(occupied bool, now uint32) {
	a.Advance(now)
	a.gun = occupied
	a.gunFiring = false
	a.gunEntered, a.gunChanged = now, now
	if !occupied {
		a.Action(0, false, 1, now)
	}
}

// MachinegunRegions evaluates the original lower-body rotation slot and
// posture + inverse(idle) + action upper-body graph, using the same11 regions.
func (a *Actor) MachinegunRegions(position Vector, rotation Vector, relativeYaw, relativePitch float32, now uint32) []world.HitRegion {
	m := a.model
	rangeAngle := m.MachinegunAngleRange
	if !a.gun || rangeAngle <= 0 {
		return nil
	}
	postureID := m.MachinegunActions[2]
	if relativePitch < 0 {
		postureID = m.MachinegunActions[3]
	}
	postureTime := float32(m.Motions[postureID].LengthMS-1) * min(1, float32(math.Abs(float64(relativePitch)))/rangeAngle)
	upper := m.motion(postureID, postureTime)
	idle := m.motion(m.MachinegunActions[0], 0)
	actionID := m.MachinegunActions[0]
	if a.gunFiring {
		actionID = m.MachinegunActions[1]
	}
	action := m.Motions[actionID]
	time := float32(now-a.gunChanged) * action.Rate
	if action.Loop {
		time = float32(math.Mod(float64(time), float64(action.LengthMS)))
	} else {
		time = min(time, float32(action.LengthMS-1))
	}
	pose := m.motion(actionID, time)
	phase := float32(math.Mod(float64(relativeYaw), float64(rangeAngle))) / rangeAngle
	if phase < 0 {
		phase++
	}
	lowerID := m.MachinegunActions[4]
	lower := m.motion(lowerID, float32(m.Motions[lowerID].LengthMS)*phase)
	for i := range pose {
		if m.Frames[i].UpperWeight == 0 {
			pose[i] = lower[i]
			continue
		}
		for axis := range 3 {
			pose[i].p[axis] = upper[i].p[axis] - idle[i].p[axis] + pose[i].p[axis]
		}
		pose[i].q = multiply(upper[i].q, multiply(conjugate(idle[i].q), pose[i].q))
	}
	if m.MachinegunTransitionMS > 0 && now-a.gunEntered < m.MachinegunTransitionMS {
		base := m.motion(a.weapon.Actions[0], a.idleTime)
		weight := float32(now-a.gunEntered) / float32(m.MachinegunTransitionMS)
		for i := range pose {
			if m.Frames[i].UpperWeight == 0 {
				base[i] = pose[i]
			}
		}
		pose = blend(base, pose, weight)
	}
	x, y, z := float64(rotation[0])*math.Pi/360, float64(rotation[1])*math.Pi/360, float64(rotation[2])*math.Pi/360
	sx, cx := math.Sincos(x)
	sy, cy := math.Sincos(y)
	sz, cz := math.Sincos(z)
	q := Rotation{float32(sx*cy*cz - cx*sy*sz), float32(cx*sy*cz + sx*cy*sz), float32(cx*cy*sz - sx*sy*cz), float32(cx*cy*cz + sx*sy*sz)}
	return m.placedRegionsRotation(pose, position, q)
}
