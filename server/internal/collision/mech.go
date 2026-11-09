package collision

import (
	"errors"
	"math"

	"nova3.test/backend/internal/world"
)

// MechModel contains collision references, not a render rig or resource names.
type MechModel struct {
	TypeID      int32         `json:"type_id"`
	Frames      []Frame       `json:"frames"`
	Regions     []BoundRegion `json:"regions"`
	Motions     []Motion      `json:"motions"`
	MotionFrame int           `json:"motion_frame"`
	AimFrame    int           `json:"aim_frame"`
	Tuning      MechTuning    `json:"tuning"`
}
type BoundRegion struct {
	Frame            int     `json:"frame"`
	A                Vector  `json:"a"`
	B                Vector  `json:"b"`
	Radius           float32 `json:"radius"`
	DamageMultiplier float32 `json:"damage_multiplier"`
}
type MechTuning struct {
	ContactDamage          float32    `json:"contact_damage"`
	MovementThreshold      float32    `json:"movement_threshold"`
	TurnThreshold          float32    `json:"turn_threshold"`
	YawStep                float32    `json:"yaw_step"`
	YawGain                [2]float32 `json:"yaw_gain"`
	StrafeAngles           [2]float32 `json:"strafe_angles"`
	ReverseAngle           float32    `json:"reverse_angle"`
	TurnAngles             [3]float32 `json:"turn_angles"`
	TransitionMS           uint32     `json:"transition_ms"`
	ActivationTransitionMS uint32     `json:"activation_transition_ms"`
	TurnTransitionMS       uint32     `json:"turn_transition_ms"`
}

const (
	mechIdle = iota
	mechInactive
	mechActivate
	mechDeactivate
	mechWalk
	mechBack
	mechLeft
	mechRight
	mechFire
	mechMissileLeft
	mechMissileRight
	mechTurnLeft15
	mechTurnRight15
	mechTurnLeft
	mechTurnRight
	mechTurnLeft90
	mechTurnRight90
	mechTurnLeft135
	mechTurnRight135
	mechMotionCount
)

func (m *MechModel) Validate() error {
	bad := errors.New("invalid mounted collision model")
	if m == nil || m.TypeID <= 0 || len(m.Frames) < 1 || len(m.Frames) > 20 || len(m.Regions) != 5 || len(m.Motions) != mechMotionCount || m.MotionFrame < 0 || m.MotionFrame >= len(m.Frames) || m.AimFrame < 0 || m.AimFrame >= len(m.Frames) {
		return bad
	}
	for i, f := range m.Frames {
		if f.Parent < -1 || f.Parent >= i || i > 0 && f.Parent < 0 || !rotationOK(f.Rotation) {
			return bad
		}
		for _, v := range f.Position {
			if !finite(v) || math.Abs(float64(v)) > 100 {
				return bad
			}
		}
	}
	for _, r := range m.Regions {
		if r.Frame < 0 || r.Frame >= len(m.Frames) || !finite(r.Radius) || r.Radius <= 0 || !finite(r.DamageMultiplier) || r.DamageMultiplier < 0 {
			return bad
		}
		for _, p := range []Vector{r.A, r.B} {
			for _, v := range p {
				if !finite(v) || math.Abs(float64(v)) > 100 {
					return bad
				}
			}
		}
	}
	for _, motion := range m.Motions {
		if motion.LengthMS == 0 || motion.LengthMS > 60000 || !finite(motion.Rate) || motion.Rate == 0 || math.Abs(float64(motion.Rate)) > 10 || motion.RootFlags & ^byte(0x23) != 0 || len(motion.Curves) != len(m.Frames) {
			return bad
		}
		for _, c := range motion.Curves {
			for kind, keys := range [][][]float32{c.Position, c.Rotation} {
				if len(keys) < 1 || len(keys) > 4096 {
					return bad
				}
				for i, key := range keys {
					if len(key) != 4+kind || key[0] < 0 || key[0] > float32(motion.LengthMS) || i == 0 && key[0] != 0 || i > 0 && key[0] <= keys[i-1][0] {
						return bad
					}
					for _, v := range key {
						if !finite(v) {
							return bad
						}
					}
					if kind == 1 && !rotationOK(Rotation(key[1:])) {
						return bad
					}
				}
			}
		}
	}
	t := m.Tuning
	if t.TransitionMS == 0 || t.ActivationTransitionMS == 0 || t.TurnTransitionMS == 0 {
		return bad
	}
	for _, v := range []float32{t.ContactDamage, t.MovementThreshold, t.TurnThreshold, t.YawStep, t.YawGain[0], t.YawGain[1], t.StrafeAngles[0], t.StrafeAngles[1], t.ReverseAngle, t.TurnAngles[0], t.TurnAngles[1], t.TurnAngles[2]} {
		if !finite(v) || v <= 0 {
			return bad
		}
	}
	return nil
}

type mechSlot struct {
	action int
	time   float32
}
type Mech struct {
	model               *MechModel
	slots               [2]mechSlot
	active              int
	blend, duration     float32
	last, observed      uint32
	position            Vector
	bodyYaw, yaw, pitch float32
	occupied, moving    bool
}

func NewMech(m *MechModel, position Vector, yaw float32, now uint32) *Mech {
	return &Mech{model: m, position: position, bodyYaw: yaw, yaw: yaw, last: now, observed: now, slots: [2]mechSlot{{action: mechInactive}, {action: mechInactive}}, blend: 1}
}
func (a *Mech) change(action int, duration uint32) {
	if a.slots[a.active].action == action {
		return
	}
	a.active = 1 - a.active
	a.slots[a.active] = mechSlot{action: action}
	a.blend = 0
	a.duration = float32(duration)
}
func (a *Mech) Occupy(occupied bool, now uint32) {
	a.Advance(now)
	if occupied == a.occupied {
		return
	}
	a.occupied = occupied
	action := mechDeactivate
	if occupied {
		action = mechActivate
	}
	a.change(action, a.model.Tuning.TransitionMS)
}
func angleDelta(v float32) float32 {
	for v > 180 {
		v -= 360
	}
	for v < -180 {
		v += 360
	}
	return v
}
func (a *Mech) Observe(position Vector, yaw, pitch float32, now uint32) {
	a.Advance(now)
	dx, dy := position[0]-a.position[0], position[1]-a.position[1]
	a.position, a.yaw, a.pitch = position, yaw, pitch
	a.observed = now
	a.moving = math.Hypot(float64(dx), float64(dy)) > float64(a.model.Tuning.MovementThreshold)
	if a.moving && a.occupied {
		length := float32(math.Hypot(float64(dx), float64(dy)))
		radians := float64(yaw) * math.Pi / 180
		forward := (-float32(math.Sin(radians))*dx + float32(math.Cos(radians))*dy) / length
		angle := float32(math.Acos(float64(min(1, max(-1, forward)))) * 180 / math.Pi)
		action := mechWalk
		if angle >= a.model.Tuning.StrafeAngles[0] && angle <= a.model.Tuning.StrafeAngles[1] {
			if float32(math.Cos(radians))*dx+float32(math.Sin(radians))*dy < 0 {
				action = mechLeft
			} else {
				action = mechRight
			}
		} else if angle > a.model.Tuning.ReverseAngle {
			action = mechBack
		}
		duration := a.model.Tuning.TransitionMS
		if active := a.slots[a.active].action; active == mechActivate || active == mechDeactivate {
			duration = a.model.Tuning.ActivationTransitionMS
		}
		a.change(action, duration)
	}
}
func (a *Mech) Fire(mode int, left bool, now uint32) {
	a.Advance(now)
	current := a.slots[a.active].action
	if current != mechIdle && (current < mechFire || current > mechMissileRight) {
		return
	}
	action := mechFire
	if mode != 0 {
		action = mechMissileRight
		if left {
			action = mechMissileLeft
		}
	}
	a.change(action, a.model.Tuning.TransitionMS)
}
func (a *Mech) StopFire(now uint32) {
	a.Advance(now)
	if a.slots[a.active].action == mechFire {
		a.change(mechIdle, a.model.Tuning.TransitionMS)
	}
}
func (a *Mech) clock(s mechSlot) float32 {
	m := a.model.Motions[s.action]
	t := min(s.time, float32(m.LengthMS-1))
	if m.Rate < 0 {
		t = float32(m.LengthMS-1) - t
	}
	return t
}
func (a *Mech) Advance(now uint32) {
	dt := now - a.last
	if dt >= 1<<31 {
		return
	}
	a.last = now
	if dt == 0 {
		return
	}
	dt = min(dt, 1000)
	a.blend = min(1, a.blend+float32(dt)/max(1, a.duration))
	finished := false
	for i := range a.slots {
		s := &a.slots[i]
		m := a.model.Motions[s.action]
		s.time += float32(dt) * float32(math.Abs(float64(m.Rate)))
		if s.time >= float32(m.LengthMS) {
			if m.Loop {
				s.time = float32(math.Mod(float64(s.time), float64(m.LengthMS)))
			} else {
				s.time = float32(m.LengthMS - 1)
				if i == a.active {
					finished = true
				}
			}
		}
	}
	current := a.slots[a.active].action
	if finished {
		next := mechIdle
		if !a.occupied {
			next = mechInactive
		}
		a.change(next, a.model.Tuning.TransitionMS)
		current = next
	}
	if !a.occupied {
		return
	}
	if now-a.observed > 100 {
		a.moving = false
	}
	if !a.moving && current >= mechWalk && current <= mechRight {
		a.change(mechIdle, a.model.Tuning.TransitionMS)
		current = mechIdle
	}
	delta := angleDelta(a.yaw - a.bodyYaw)
	if current == mechIdle && float32(math.Abs(float64(delta))) >= a.model.Tuning.TurnThreshold {
		index := 0
		for index < 3 && float32(math.Abs(float64(delta))) > a.model.Tuning.TurnAngles[index] {
			index++
		}
		action := mechTurnLeft15 + 2*index
		if delta < 0 {
			action++
		}
		a.change(action, a.model.Tuning.TurnTransitionMS)
	}
	// The native remote controller turns the world body separately from the
	// animated upper-body look offset. Keep both angles instead of pitching legs.
	if current != mechActivate && current != mechDeactivate {
		gain := a.model.Tuning.YawGain[0]
		if delta < 0 {
			gain = a.model.Tuning.YawGain[1]
		}
		step := a.model.Tuning.YawStep * float32(dt) * .03
		a.bodyYaw += min(step, max(-step, delta*gain))
	}
}
func (a *Mech) pose(s mechSlot) []localPose {
	m := a.model.Motions[s.action]
	time := a.clock(s)
	result := make([]localPose, len(a.model.Frames))
	for i, c := range m.Curves {
		result[i] = localPose{Vector(sample(c.Position, time, false)), Rotation(sample(c.Rotation, time, true))}
	}
	root := a.model.MotionFrame
	for axis := range 3 {
		if m.RootFlags&(1<<axis) != 0 {
			result[root].p[axis] = m.Curves[root].Position[0][axis+1]
		}
	}
	if m.RootFlags&0x20 != 0 {
		// Remove the authored Z twist from the motion reference; the world body
		// owns turning, while the remaining swing stays on the animated frame.
		initial := Rotation(m.Curves[root].Rotation[0][1:])
		delta := multiply(conjugate(initial), result[root].q)
		n := float32(math.Hypot(float64(delta[2]), float64(delta[3])))
		if n > 0 {
			twist := Rotation{0, 0, delta[2] / n, delta[3] / n}
			result[root].q = multiply(result[root].q, conjugate(twist))
		}
	}
	return result
}
func (a *Mech) Regions() []world.HitRegion {
	return a.PlaceRegions(a.model.Regions)
}

// PlaceRegions shares the live attachment pose with numeric interaction volumes.
func (a *Mech) PlaceRegions(regions []BoundRegion) []world.HitRegion {
	pose := blend(a.pose(a.slots[1-a.active]), a.pose(a.slots[a.active]), a.blend)
	current := a.slots[a.active].action
	if a.occupied && current != mechActivate && current != mechDeactivate {
		pitch := -angleDelta(a.pitch) * float32(math.Pi/360)
		yaw := angleDelta(a.yaw-a.bodyYaw) * float32(math.Pi/360)
		q := Rotation{float32(math.Sin(float64(pitch))) * float32(math.Cos(float64(yaw))), -float32(math.Sin(float64(pitch))) * float32(math.Sin(float64(yaw))), float32(math.Cos(float64(pitch))) * float32(math.Sin(float64(yaw))), float32(math.Cos(float64(pitch))) * float32(math.Cos(float64(yaw)))}
		frame := a.model.AimFrame
		pose[frame].q = multiply(pose[frame].q, q)
	}
	yaw := a.bodyYaw * float32(math.Pi/360)
	root := localMatrix(localPose{a.position, Rotation{0, 0, float32(math.Sin(float64(yaw))), float32(math.Cos(float64(yaw)))}})
	global := make([]transform, len(pose))
	for i, p := range pose {
		parent := root
		if a.model.Frames[i].Parent >= 0 {
			parent = global[a.model.Frames[i].Parent]
		}
		global[i] = compose(parent, localMatrix(p))
	}
	parts := make([]world.HitRegion, len(regions))
	for i, r := range regions {
		place := func(v Vector) world.Vec3 {
			m := global[r.Frame]
			return world.Vec3{X: float64(m[0][0]*v[0] + m[1][0]*v[1] + m[2][0]*v[2] + m[3][0]), Y: float64(m[0][1]*v[0] + m[1][1]*v[1] + m[2][1]*v[2] + m[3][1]), Z: float64(m[0][2]*v[0] + m[1][2]*v[1] + m[2][2]*v[2] + m[3][2])}
		}
		parts[i] = world.HitRegion{PartID: uint16(i + 1), Volume: world.Capsule{A: place(r.A), B: place(r.B), Radius: float64(r.Radius)}, DamageMultiplier: r.DamageMultiplier}
	}
	return parts
}
