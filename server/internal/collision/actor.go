package collision

import (
	"math"
	"nova3.test/backend/internal/world"
)

type slot struct {
	motion int
	time   float32
	rate   float32
}

// Actor owns collision clocks and action transitions for one admitted life.
// Input positions remain the existing authenticated movement proposals.
type Actor struct {
	model                         *Model
	weapon                        Weapon
	last, observed                uint32
	position, velocity            Vector
	initialized                   bool
	walkPhase, runPhase, idleTime float32
	lower, upper                  float32
	active, action                int
	slots                         [2]slot
	aimed                         bool
	gun, gunFiring                bool
	gunEntered, gunChanged        uint32
}

func NewActor(m *Model, weapon int32, now uint32) *Actor {
	w, ok := m.Weapon(weapon)
	if !ok {
		return nil
	}
	a := &Actor{model: m, weapon: w, last: now, observed: now}
	for i := range a.slots {
		id := w.Actions[0]
		a.slots[i] = slot{id, 0, m.Motions[id].Rate}
	}
	return a
}
func (a *Actor) Weapon() int32 { return a.weapon.TypeID }

// SelectWeapon follows RemotePlayer.SetCurrentWeapon: rebind the upper-body
// idle/aim state without replacing the actor or its locomotion/seat controllers.
func (a *Actor) SelectWeapon(id int32, now uint32) bool {
	w, ok := a.model.Weapon(id)
	if !ok {
		return false
	}
	if w.TypeID == a.weapon.TypeID {
		return true
	}
	a.Advance(now)
	a.weapon = w
	if !a.gun {
		action := 0
		if a.aimed {
			action = 1
		}
		a.Action(action, a.aimed, 1, now)
	}
	return true
}
func (a *Actor) Observe(position Vector, now uint32) {
	a.Advance(now)
	if a.initialized && now != a.observed {
		dt := float32(now-a.observed) * .001
		for i := range 3 {
			a.velocity[i] = (position[i] - a.position[i]) / dt
		}
	} else {
		a.velocity = Vector{}
	}
	a.position, a.observed, a.initialized = position, now, true
}
func (a *Actor) moving(now uint32) bool {
	return now-a.observed <= 100 && (a.velocity[0] != 0 || a.velocity[1] != 0)
}
func approach(value, target, step float32) float32 {
	if value < target {
		return min(value+step, target)
	}
	return max(value-step, target)
}
func (a *Actor) Action(action int, aimed bool, reloadRate float32, now uint32) {
	a.Advance(now)
	if action < 0 || action >= 7 {
		return
	}
	if a.gun {
		firing := action == 4 || action == 5
		if firing != a.gunFiring {
			a.gunChanged = now
		}
		a.gunFiring = firing
		return
	}
	// Sustained fire keeps its native loop; each projectile is not a new action.
	if (action == 4 || action == 5) && action == a.action {
		a.aimed = aimed
		return
	}
	a.active = 1 - a.active
	a.action, a.aimed = action, aimed
	id := a.weapon.Actions[action]
	rate := a.model.Motions[id].Rate
	if action == 2 || action == 3 {
		rate *= reloadRate
	}
	a.slots[a.active] = slot{id, 0, rate}
}
func (a *Actor) Advance(now uint32) {
	if a == nil || int32(now-a.last) <= 0 {
		return
	}
	if a.gun {
		a.last = now
		return
	}
	dt := now - a.last
	a.last = now
	// Long pauses do not replay unbounded historical simulation work.
	dt = min(dt, 1000)
	m := a.model
	speed := float32(math.Hypot(float64(a.velocity[0]), float64(a.velocity[1])))
	idle := m.Motions[a.weapon.Actions[0]]
	a.idleTime = float32(math.Mod(float64(a.idleTime+float32(uint32(float32(dt)*idle.Rate))), float64(idle.LengthMS)))
	speed = min(max(m.SpeedScale*speed+m.SpeedOffset, m.SpeedOffset), m.MaximumSpeed)
	walkLength := float32(m.Motions[m.Locomotion[0]].LengthMS)
	runLength := float32(m.Motions[m.Locomotion[4]].LengthMS)
	var walkRate, runRate float32
	switch {
	case speed <= m.WalkSpeed:
		walkRate = speed / m.WalkSpeed
		runRate = walkRate * runLength / walkLength
	case speed > m.RunSpeed:
		runRate = speed / m.RunSpeed
		walkRate = runRate * walkLength / runLength
	default:
		f := (speed - m.WalkSpeed) / (m.RunSpeed - m.WalkSpeed)
		ratio := walkLength / runLength
		walkRate = 1 + (ratio-1)*f
		runRate = (1-f)/ratio + f
	}
	// The master's nearest previous child supplies normalized phase on updates.
	phase := a.walkPhase
	if speed > (m.WalkSpeed+m.RunSpeed)*.5 {
		phase = a.runPhase
	}
	a.walkPhase = float32(math.Mod(float64(phase+float32(uint32(float32(dt)*walkRate))/walkLength), 1))
	a.runPhase = float32(math.Mod(float64(phase+float32(uint32(float32(dt)*runRate))/runLength), 1))
	target := float32(0)
	if a.moving(now) {
		target = 1
	}
	step := float32(dt) / float32(m.TransitionMS)
	a.lower = approach(a.lower, target, step)
	a.upper = approach(a.upper, float32(a.active), step)
	finished := false
	for i := range a.slots {
		s := &a.slots[i]
		motion := m.Motions[s.motion]
		s.time += float32(uint32(float32(dt) * s.rate))
		if s.time >= float32(motion.LengthMS) {
			if motion.Loop {
				s.time = float32(math.Mod(float64(s.time), float64(motion.LengthMS)))
			} else {
				s.time = float32(motion.LengthMS - 1)
				if i == a.active {
					finished = true
				}
			}
		}
	}
	if finished && (a.action >= 2 && a.action <= 6 || a.action == 8 || a.action == 9) {
		action := 0
		if a.aimed {
			action = 1
		}
		a.Action(action, a.aimed, 1, now)
	}
}

func (a *Actor) Power(kind byte, now uint32) {
	if a.gun {
		return
	}
	if kind == 7 {
		a.Action(6, false, 1, now)
		return
	}
	index := 1
	if kind == 17 {
		index = 0
	} else if kind == 2 {
		index = 2
	}
	a.Advance(now)
	a.active = 1 - a.active
	a.action = 7 + index
	id := a.model.PowerActions[index]
	a.slots[a.active] = slot{id, 0, a.model.Motions[id].Rate}
}
func direction(x, y float32) (int, int, float32) {
	length := float32(math.Sqrt(float64(x*x + y*y)))
	angle := float32(0)
	if length > 0 {
		angle = float32(math.Acos(float64(min(max(y/length, -1), 1)))) * float32(180/math.Pi)
		if x > 0 {
			angle = 360 - angle
		}
	}
	first := min(int(angle/90), 3)
	return first, (first + 1) % 4, (angle - float32(first*90)) / 90
}
func (a *Actor) Regions(position Vector, yaw, pitch float32, now uint32) []world.HitRegion {
	if a == nil {
		return nil
	}
	a.Advance(now)
	m := a.model
	angle := float64(yaw) * math.Pi / 180
	cos, sin := float32(math.Cos(angle)), float32(math.Sin(angle))
	x, y := a.velocity[0]*cos+a.velocity[1]*sin, -a.velocity[0]*sin+a.velocity[1]*cos
	first, second, weight := direction(x, y)
	walkA, walkB := m.Locomotion[first], m.Locomotion[second]
	runA, runB := m.Locomotion[4+first], m.Locomotion[4+second]
	walk := blend(m.motion(walkA, float32(uint32(a.walkPhase*float32(m.Motions[walkA].LengthMS)))), m.motion(walkB, float32(uint32(a.walkPhase*float32(m.Motions[walkB].LengthMS)))), weight)
	run := blend(m.motion(runA, float32(uint32(a.runPhase*float32(m.Motions[runA].LengthMS)))), m.motion(runB, float32(uint32(a.runPhase*float32(m.Motions[runB].LengthMS)))), weight)
	speed := min(max(m.SpeedScale*float32(math.Hypot(float64(x), float64(y)))+m.SpeedOffset, m.SpeedOffset), m.MaximumSpeed)
	locomotion := blend(walk, run, min(max((speed-m.WalkSpeed)/(m.RunSpeed-m.WalkSpeed), 0), 1))
	idle := m.motion(a.weapon.Actions[0], a.idleTime)
	lower := blend(idle, locomotion, a.lower)
	upper := blend(m.motion(a.slots[0].motion, a.slots[0].time), m.motion(a.slots[1].motion, a.slots[1].time), a.upper)
	for i, f := range m.Frames {
		if f.UpperWeight == 1 {
			lower[i] = upper[i]
		}
	}
	return m.regions(lower, position, yaw, pitch)
}
