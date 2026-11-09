// Package collision evaluates the reduced, numeric player collision model.
// It has no dependency on original resources, scene rendering or native code.
package collision

import (
	"errors"
	"math"
	"sort"

	"nova3.test/backend/internal/world"
)

type Vector [3]float32
type Rotation [4]float32
type Frame struct {
	Parent      int      `json:"parent"`
	Position    Vector   `json:"position"`
	Rotation    Rotation `json:"rotation"`
	UpperWeight float32  `json:"upper_weight"`
}
type Region struct {
	Part             uint16  `json:"part"`
	Frame            int     `json:"frame"`
	Radius           float32 `json:"radius"`
	HalfLength       float32 `json:"half_length"`
	Offset           float32 `json:"offset"`
	DamageMultiplier float32 `json:"damage_multiplier"`
}
type Curve struct {
	Position [][]float32 `json:"position"`
	Rotation [][]float32 `json:"rotation"`
}
type Motion struct {
	LengthMS  uint32  `json:"length_ms"`
	Rate      float32 `json:"rate"`
	Loop      bool    `json:"loop"`
	RootFlags byte    `json:"root_flags"`
	Curves    []Curve `json:"curves"`
}
type Weapon struct {
	TypeID  int32  `json:"type_id"`
	Actions [7]int `json:"actions"`
}
type Model struct {
	Frames                 []Frame  `json:"frames"`
	Regions                []Region `json:"regions"`
	Motions                []Motion `json:"motions"`
	Weapons                []Weapon `json:"weapons"`
	Locomotion             [8]int   `json:"locomotion"`
	AimFrames              [6]int   `json:"aim_frames"`
	WalkSpeed              float32  `json:"walk_speed"`
	RunSpeed               float32  `json:"run_speed"`
	SpeedScale             float32  `json:"speed_scale"`
	SpeedOffset            float32  `json:"speed_offset"`
	MaximumSpeed           float32  `json:"maximum_speed"`
	TransitionMS           uint32   `json:"transition_ms"`
	ShotRadius             float32  `json:"shot_radius"`
	MachinegunActions      [5]int   `json:"machinegun_actions"`
	MachinegunTransitionMS uint32   `json:"machinegun_transition_ms"`
	MachinegunAngleRange   float32  `json:"machinegun_angle_range"`
	PowerActions           [3]int   `json:"power_actions"`
}

func finite(v float32) bool { return !math.IsNaN(float64(v)) && !math.IsInf(float64(v), 0) }
func rotationOK(q Rotation) bool {
	var n float32
	for _, v := range q {
		if !finite(v) {
			return false
		}
		n += v * v
	}
	return math.Abs(float64(n-1)) < .002
}
func (m *Model) Validate() error {
	bad := errors.New("invalid reduced collision model")
	if m == nil || len(m.Frames) < 1 || len(m.Frames) > 20 || len(m.Regions) != 11 || len(m.Motions) < 1 || len(m.Motions) > 128 || len(m.Weapons) < 1 || len(m.Weapons) > 32 {
		return bad
	}
	for i, f := range m.Frames {
		if f.Parent < -1 || f.Parent >= i || (i > 0 && f.Parent < 0) || !rotationOK(f.Rotation) || (f.UpperWeight != 0 && f.UpperWeight != 1) {
			return bad
		}
		for _, v := range f.Position {
			if !finite(v) || math.Abs(float64(v)) > 100 {
				return bad
			}
		}
	}
	seen := map[uint16]bool{}
	for _, r := range m.Regions {
		if r.Frame < 0 || r.Frame >= len(m.Frames) || r.Part == 0 || r.Part > 15 || seen[r.Part] {
			return bad
		}
		seen[r.Part] = true
		for _, v := range []float32{r.Radius, r.HalfLength, r.Offset, r.DamageMultiplier} {
			if !finite(v) || v < 0 || v > 100 {
				return bad
			}
		}
		if r.Radius == 0 {
			return bad
		}
	}
	for index, motion := range m.Motions {
		if motion.LengthMS == 0 || motion.LengthMS > 60000 || !finite(motion.Rate) || motion.Rate < 0 || motion.Rate > 10 || motion.RootFlags & ^byte(7) != 0 || len(motion.Curves) != len(m.Frames) {
			return bad
		}
		if motion.Rate == 0 && (m.MachinegunAngleRange <= 0 || index != m.PowerActions[0] && index != m.MachinegunActions[2] && index != m.MachinegunActions[3] && index != m.MachinegunActions[4]) {
			return bad
		}
		for _, c := range motion.Curves {
			for kind, keys := range [][][]float32{c.Position, c.Rotation} {
				if len(keys) == 0 || len(keys) > 4096 {
					return bad
				}
				for i, key := range keys {
					if len(key) != 4+kind || key[0] < 0 || key[0] > float32(motion.LengthMS) || (i == 0 && key[0] != 0) || (i > 0 && key[0] <= keys[i-1][0]) {
						return bad
					}
					for _, v := range key {
						if !finite(v) || math.Abs(float64(v)) > 1000000 {
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
	ids := map[int32]bool{}
	for _, w := range m.Weapons {
		if w.TypeID < 0 || ids[w.TypeID] {
			return bad
		}
		ids[w.TypeID] = true
		for _, id := range w.Actions {
			if id < 0 || id >= len(m.Motions) {
				return bad
			}
		}
	}
	for _, id := range m.Locomotion {
		if id < 0 || id >= len(m.Motions) {
			return bad
		}
	}
	for _, id := range m.MachinegunActions {
		if id < 0 || id >= len(m.Motions) {
			return bad
		}
	}
	for _, id := range m.PowerActions {
		if id < 0 || id >= len(m.Motions) {
			return bad
		}
	}
	if m.MachinegunAngleRange != 0 || m.MachinegunTransitionMS != 0 {
		if !finite(m.MachinegunAngleRange) || m.MachinegunAngleRange <= 0 || m.MachinegunAngleRange > 180 || m.MachinegunTransitionMS == 0 || m.MachinegunTransitionMS > 2000 {
			return bad
		}
	}
	for _, id := range m.AimFrames {
		if id < 0 || id >= len(m.Frames) {
			return bad
		}
	}
	for _, v := range []float32{m.WalkSpeed, m.RunSpeed, m.SpeedScale, m.SpeedOffset, m.MaximumSpeed, m.ShotRadius} {
		if !finite(v) || v <= 0 {
			return bad
		}
	}
	if m.WalkSpeed >= m.RunSpeed || m.MaximumSpeed < m.RunSpeed || m.TransitionMS == 0 || m.TransitionMS > 1000 || m.ShotRadius > 1 {
		return bad
	}
	return nil
}

func (m *Model) Weapon(id int32) (Weapon, bool) {
	if m != nil {
		for _, w := range m.Weapons {
			if w.TypeID == id {
				return w, true
			}
		}
	}
	return Weapon{}, false
}
func multiply(a, b Rotation) Rotation {
	x, y, z, w := a[0], a[1], a[2], a[3]
	X, Y, Z, W := b[0], b[1], b[2], b[3]
	return Rotation{((w*X + x*W) + y*Z) - z*Y, ((w*Y + y*W) + z*X) - x*Z, ((w*Z + z*W) + x*Y) - y*X, ((w*W - x*X) - y*Y) - z*Z}
}
func conjugate(q Rotation) Rotation { return Rotation{-q[0], -q[1], -q[2], q[3]} }
func rotate(q Rotation, v Vector) Vector {
	x, y, z, w := q[0], q[1], q[2], q[3]
	return Vector{(1-2*(y*y+z*z))*v[0] + 2*(x*y-z*w)*v[1] + 2*(x*z+y*w)*v[2],
		2*(x*y+z*w)*v[0] + (1-2*(x*x+z*z))*v[1] + 2*(y*z-x*w)*v[2],
		2*(x*z-y*w)*v[0] + 2*(y*z+x*w)*v[1] + (1-2*(x*x+y*y))*v[2]}
}
func slerp(a, b Rotation, t float32) Rotation {
	dot := ((a[0]*b[0] + a[1]*b[1]) + a[2]*b[2]) + a[3]*b[3]
	if dot < 0 {
		for i := range a {
			a[i] = -a[i]
		}
		dot = -dot
	}
	if dot > .95 {
		var q Rotation
		var n float32
		for i := range q {
			q[i] = a[i] + (b[i]-a[i])*t
			n += q[i] * q[i]
		}
		if n > 0 {
			inverse := float32(1) / float32(math.Sqrt(float64(n)))
			for i := range q {
				q[i] *= inverse
			}
		}
		return q
	}
	angle := float32(math.Acos(float64(min(max(dot, -1), 1))))
	inv := float32(1) / float32(math.Sin(float64(angle)))
	wa := float32(math.Sin(float64((1-t)*angle))) * inv
	wb := float32(math.Sin(float64(t*angle))) * inv
	var q Rotation
	for i := range q {
		q[i] = a[i]*wa + b[i]*wb
	}
	return q
}
func sample(keys [][]float32, time float32, rotation bool) []float32 {
	i := sort.Search(len(keys), func(i int) bool { return keys[i][0] > time }) - 1
	if i < 0 {
		i = 0
	}
	if i == len(keys)-1 {
		return keys[i][1:]
	}
	t := min(max((time-keys[i][0])/(keys[i+1][0]-keys[i][0]), 0), 1)
	if t == 0 {
		return keys[i][1:]
	}
	if t == 1 {
		return keys[i+1][1:]
	}
	if rotation {
		q := slerp(Rotation(keys[i][1:]), Rotation(keys[i+1][1:]), t)
		return q[:]
	}
	v := make([]float32, 3)
	for j := range v {
		v[j] = keys[i][j+1] + (keys[i+1][j+1]-keys[i][j+1])*t
	}
	return v
}

type localPose struct {
	p Vector
	q Rotation
}

func (m *Model) motion(id int, time float32) []localPose {
	motion := m.Motions[id]
	out := make([]localPose, len(m.Frames))
	for i, c := range motion.Curves {
		out[i] = localPose{Vector(sample(c.Position, time, false)), Rotation(sample(c.Rotation, time, true))}
		if i == 0 {
			for axis := range 3 {
				if motion.RootFlags&(1<<axis) != 0 {
					out[i].p[axis] = c.Position[0][axis+1]
				}
			}
		}
	}
	return out
}
func blend(a, b []localPose, t float32) []localPose {
	if t == 0 {
		return a
	}
	if t == 1 {
		return b
	}
	out := make([]localPose, len(a))
	for i := range out {
		for axis := range 3 {
			out[i].p[axis] = a[i].p[axis]*(1-t) + b[i].p[axis]*t
		}
		out[i].q = slerp(a[i].q, b[i].q, t)
	}
	return out
}

type transform [4]Vector

func localMatrix(p localPose) transform {
	x, y, z, w := p.q[0], p.q[1], p.q[2], p.q[3]
	xx, yy, zz := 2*(x*x), 2*(y*y), 2*(z*z)
	xy, xz, yz := 2*(x*y), 2*(x*z), 2*(y*z)
	wx, wy, wz := 2*(w*x), 2*(w*y), 2*(w*z)
	return transform{{(1 - yy) - zz, xy + wz, xz - wy}, {xy - wz, (1 - xx) - zz, yz + wx}, {xz + wy, yz - wx, (1 - xx) - yy}, p.p}
}
func compose(parent, child transform) transform {
	var out transform
	for i, column := range child {
		for axis := range 3 {
			out[i][axis] = (parent[0][axis]*column[0] + parent[1][axis]*column[1]) + parent[2][axis]*column[2]
			if i == 3 {
				out[i][axis] += parent[3][axis]
			}
		}
	}
	return out
}

// Original rigid-body placement derives and normalizes rotation from the
// composed scene matrix; multiplying local quaternions skips that operation.
func matrixRotation(m transform) Rotation {
	var q Rotation
	trace := (m[0][0] + m[1][1]) + m[2][2]
	if trace > 0 {
		s := float32(math.Sqrt(float64(trace + 1)))
		inverse := float32(.5) / s
		q = Rotation{(m[1][2] - m[2][1]) * inverse, (m[2][0] - m[0][2]) * inverse, (m[0][1] - m[1][0]) * inverse, s * .5}
	} else {
		i := 0
		if !(m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
			if m[1][1] > m[2][2] {
				i = 1
			} else {
				i = 2
			}
		}
		j, k := (i+1)%3, (i+2)%3
		s := float32(math.Sqrt(float64(((m[i][i] - m[j][j]) - m[k][k]) + 1)))
		inverse := float32(.5) / s
		q[i] = s * .5
		q[j] = (m[i][j] + m[j][i]) * inverse
		q[k] = (m[i][k] + m[k][i]) * inverse
		q[3] = (m[j][k] - m[k][j]) * inverse
	}
	n := ((q[1]*q[1] + q[2]*q[2]) + q[3]*q[3]) + q[0]*q[0]
	if n > 0 {
		inverse := float32(1) / float32(math.Sqrt(float64(n)))
		for i := range q {
			q[i] *= inverse
		}
	}
	return q
}
func (m *Model) regions(pose []localPose, position Vector, yaw, pitch float32) []world.HitRegion {
	if pitch > 180 {
		pitch -= 360
	}
	if pitch < -180 {
		pitch += 360
	}
	for index := 1; index < 6; index++ {
		node := m.AimFrames[index]
		parent := pose[m.AimFrames[0]].q
		for ancestor := 1; ancestor < min(index, 4); ancestor++ {
			parent = multiply(parent, pose[m.AimFrames[ancestor]].q)
		}
		fraction := float32(.2)
		if index == 1 {
			fraction = .4
		}
		angle := pitch * fraction * float32(math.Pi/180) * .5
		offset := Rotation{float32(math.Sin(float64(angle))), 0, 0, float32(math.Cos(float64(angle)))}
		pose[node].q = multiply(conjugate(parent), multiply(offset, multiply(parent, pose[node].q)))
	}
	return m.placedRegions(pose, position, yaw)
}
func (m *Model) placedRegions(pose []localPose, position Vector, yaw float32) []world.HitRegion {
	yaw *= float32(math.Pi/180) * .5
	return m.placedRegionsRotation(pose, position, Rotation{0, 0, float32(math.Sin(float64(yaw))), float32(math.Cos(float64(yaw)))})
}
func (m *Model) placedRegionsRotation(pose []localPose, position Vector, rotation Rotation) []world.HitRegion {
	root := localMatrix(localPose{position, rotation})
	global := make([]transform, len(pose))
	for i, local := range pose {
		parent := root
		if m.Frames[i].Parent >= 0 {
			parent = global[m.Frames[i].Parent]
		}
		global[i] = compose(parent, localMatrix(local))
	}
	out := make([]world.HitRegion, len(m.Regions))
	for i, r := range m.Regions {
		frame := global[r.Frame]
		axis := rotate(matrixRotation(frame), Vector{1, 0, 0})
		var a, b Vector
		for j := range 3 {
			center := frame[3][j] + axis[j]*r.Offset
			a[j] = center - axis[j]*r.HalfLength
			b[j] = center + axis[j]*r.HalfLength
		}
		out[i] = world.HitRegion{PartID: r.Part, DamageMultiplier: r.DamageMultiplier, Volume: world.Capsule{A: world.Vec3{X: float64(a[0]), Y: float64(a[1]), Z: float64(a[2])}, B: world.Vec3{X: float64(b[0]), Y: float64(b[1]), Z: float64(b[2])}, Radius: float64(r.Radius)}}
	}
	return out
}
