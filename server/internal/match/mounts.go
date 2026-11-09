package match

import (
	"encoding/binary"
	"math"
	"nova3.test/backend/internal/authority"
	"nova3.test/backend/internal/collision"
	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/world"
	"time"
)

type mountState struct {
	definition                 authority.Mount
	position                   world.Vec3
	health                     float32
	hitRemainder               float32
	occupants                  [2]*peer
	state, shooter, kinematics []byte
	weapons                    [2]*weaponState
	shots                      [2]int32
	rotation                   [3]float64
	respawnAt                  time.Time
	velocity                   world.Vec3
	lastPose                   time.Time
	contacts                   map[byte]uint32
	mech                       *collision.Mech
	generation                 uint32
}

func (s *Session) authorizeMountedShot(p *peer, v game.Shot, now time.Time) (world.Vec3, world.Vec3, *weaponState, bool) {
	empty := world.Vec3{}
	p.shotReject = 20
	mount := p.mount.vehicle
	if mount.definition.Category == 1 && p.mount.seat != 1 {
		return empty, empty, nil, false
	}
	if v.SourceType != 3 && !(v.SourceType == 0 && v.SourceIndex == p.player.Slot) && !(v.SourceType == 1 && mount.definition.StaticWireIndex >= 12 && v.SourceIndex == mount.definition.StaticWireIndex) {
		p.shotReject = 24
		return empty, empty, nil, false
	}
	definition, ok := s.authority.MountedWeapon(mount.definition.WeaponTypeID)
	mode := int(v.Flags & 1)
	if !ok || v.Weapon != definition.WireIndex || mode >= len(definition.FireModes) {
		return empty, empty, nil, false
	}
	if mount.weapons[mode] == nil {
		mount.weapons[mode] = &weaponState{definition: definition, modifiers: authority.WeaponModifiers{AmmoCapacity: 1, FireSpeed: 1, LauncherDelay: 1, ReloadSpeed: 1, Spread: 1, Damage: 1}}
	}
	w := mount.weapons[mode]
	age := int32(s.elapsed(now) - v.Time)
	p.shotReject = 21
	if age < -2000 || age > 2000 || int32(v.Time-p.life.bornAt) < 0 || w.haveShot && int32(v.Time-w.lastShot) <= 0 {
		return empty, empty, nil, false
	}
	origin, err := s.movement.bounds.DecodePosition(v.Origin)
	if err != nil {
		return empty, empty, nil, false
	}
	direction, err := world.DecodeShotDirection(v.Direction)
	if err != nil {
		return empty, empty, nil, false
	}
	p.shotReject = 22
	mountPosition := mount.position
	if past, ok := s.historicalMount(mount, s.confirmationTime(p, v.Time, now)); ok {
		mountPosition = past.position
	}
	offset := origin.Sub(mountPosition)
	radius := float64(mount.definition.Radius + mount.definition.Height)
	if offset.Dot(offset) > radius*radius {
		return empty, empty, nil, false
	}
	clearanceOrigin := mountPosition
	if anchor := mount.definition.Gunner; mount.definition.Category == 3 && anchor != nil {
		// A fixed gun's placement origin is its base at floor level. Native
		// firing belongs to the elevated body pivot; tracing from the base
		// crosses supporting map geometry before reaching a valid muzzle.
		clearanceOrigin = world.Vec3{X: float64(anchor.Pivot[0]), Y: float64(anchor.Pivot[1]), Z: float64(anchor.Pivot[2])}
	}
	if _, blocked, err := s.movement.scene.Raycast(clearanceOrigin, origin, world.Filter{Group: 0x401, Mask: 0x76f7}, &mount.definition.ObjectID); err != nil || blocked {
		p.shotReject = 27
		return empty, empty, nil, false
	}
	interval, err := definition.FireInterval(mode, w.modifiers)
	p.shotReject = 23
	if err != nil || !w.fire.Allows(now, interval) || !s.burstAllows(w, mode, now) || mount.shots[mode] >= definition.MagazineAmmo {
		return empty, empty, nil, false
	}
	w.fire.Commit(now, interval)
	s.commitBurst(w, mode, now)
	w.haveShot = true
	w.lastShot = v.Time
	mount.shots[mode]++
	if mount.mech != nil {
		yaw := pc.DecodeAngle(s.movement.poses[p.player.Slot].Yaw) * math.Pi / 180
		left := offset.X*math.Cos(yaw)+offset.Y*math.Sin(yaw) < 0
		mount.mech.Fire(mode, left, s.elapsed(now))
	}
	p.life.shots++
	p.shotReject = 0
	return origin, direction, w, true
}

type mountSeat struct {
	vehicle *mountState
	seat    byte
}

func (s *Session) resetMounts(now time.Time, out *[]Delivery) error {
	for _, v := range s.mounts {
		v.generation++
		v.weapons, v.shots = [2]*weaponState{}, [2]int32{}
		if v.definition.Category == 3 {
			// Fixed guns have no vehicle-respawn message, but their weapon
			// state must start fresh with the native round weapon reset too.
			continue
		}
		v.position = world.Vec3{X: float64(v.definition.Position[0]), Y: float64(v.definition.Position[1]), Z: float64(v.definition.Position[2])}
		v.health = v.definition.Health
		v.hitRemainder = 0
		for axis := range v.rotation {
			v.rotation[axis] = float64(v.definition.Rotation[axis])
		}
		v.respawnAt = time.Time{}
		v.velocity, v.lastPose, v.contacts = world.Vec3{}, time.Time{}, nil
		v.occupants = [2]*peer{}
		v.state, v.shooter, v.kinematics = nil, nil, nil
		s.resetMech(v, now)
		kind := byte(136) // Native vehicle respawn restores its authored transform.
		if v.definition.Category == 2 {
			kind = 145
		}
		if err := s.announce([]byte{kind, v.definition.WireIndex}, now, out); err != nil {
			return err
		}
	}
	return nil
}

func (s *Session) mountByIndex(category, index byte) *mountState {
	for _, v := range s.mounts {
		if v.definition.Category == category && v.definition.WireIndex == index {
			return v
		}
	}
	return nil
}
func (s *Session) announce(message []byte, now time.Time, out *[]Delivery) error {
	for _, p := range s.peers {
		if p.sceneReady && !p.departed {
			if err := s.emit(p, message, true, now, out); err != nil {
				return err
			}
		}
	}
	return nil
}
func (s *Session) mountGrant(p *peer, v *mountState, seat byte, now time.Time, out *[]Delivery) error {
	if v == nil || p.life.dead || p.life.frozen || p.roundSpawnPending || v.definition.Category != 1 && seat != 0 || seat > 1 {
		return nil
	}
	if v.definition.Category != 3 && v.health <= 0 {
		return nil
	}
	if occupant := v.occupants[seat]; occupant != nil && occupant != p {
		return nil
	}
	// FFA gives each actor a distinct team. Original IsPostAvailableForme
	// prevents opposing actors from sharing driver/shooter seats.
	if occupant := v.occupants[1-seat]; occupant != nil && occupant != p && occupant.player.Beacon[0] != p.player.Beacon[0] {
		return nil
	}
	if p.mount == nil && !s.canReachMount(p, v) {
		return nil
	}
	if p.mount != nil {
		if p.mount.vehicle != v {
			return nil
		}
		if p.mount.seat == seat {
			return nil
		}
		p.mount.vehicle.occupants[p.mount.seat] = nil
	}
	p.mount = &mountSeat{vehicle: v, seat: seat}
	v.occupants[seat] = p
	if v.mech != nil {
		v.mech.Occupy(true, s.elapsed(now))
	}
	if v.definition.Category == 3 {
		if a := s.collider(p, now); a != nil {
			a.SetMachinegun(true, s.elapsed(now))
		}
	}
	s.recordHitFrame(now)
	return s.announce(mountGrantMessage(p), now, out)
}
func mountGrantMessage(p *peer) []byte {
	v := p.mount.vehicle
	switch v.definition.Category {
	case 1:
		return []byte{game.EnterVehicle, p.player.Slot, v.definition.WireIndex, p.mount.seat}
	case 2:
		return []byte{game.EnterMech, p.player.Slot, v.definition.WireIndex}
	default:
		m := []byte{game.EnterMachinegun, p.player.Slot, 0, 0, 0, 0}
		binary.BigEndian.PutUint32(m[2:], v.definition.ObjectID)
		return m
	}
}
func (s *Session) leaveMount(p *peer, now time.Time, out *[]Delivery) error {
	if p.mount == nil {
		return nil
	}
	v := p.mount.vehicle
	message := []byte{game.ExitVehicle, p.player.Slot, v.definition.WireIndex, 255}
	if v.definition.Category == 2 {
		message = []byte{game.ExitMech, p.player.Slot, v.definition.WireIndex}
	}
	if v.definition.Category == 3 {
		message = []byte{game.ExitMachinegun, p.player.Slot}
	}
	v.occupants[p.mount.seat] = nil
	if v.mech != nil {
		v.mech.Occupy(false, s.elapsed(now))
	}
	if v.definition.Category == 3 {
		if a := s.collider(p, now); a != nil {
			a.SetMachinegun(false, s.elapsed(now))
		}
	}
	p.lastMount, p.lastMountUntil = v, now.Add(2*time.Second)
	p.mount = nil
	s.recordHitFrame(now)
	return s.announce(message, now, out)
}

func (s *Session) zoneObjectOwned(p *peer, object byte, now time.Time) bool {
	if object == p.player.Slot {
		return true
	}
	owned := func(v *mountState) bool {
		if v == nil || object < 12 {
			return false
		}
		return object == v.definition.StaticWireIndex || object == v.definition.ProxyWireIndices[0] || object == v.definition.ProxyWireIndices[1]
	}
	return p.mount != nil && owned(p.mount.vehicle) || now.Before(p.lastMountUntil) && owned(p.lastMount)
}
func (s *Session) mountIntent(p *peer, m []byte, reliable bool, now time.Time, out *[]Delivery) error {
	if s.authority == nil || s.movement == nil {
		return nil
	}
	switch m[0] {
	case game.RequestEnterVehicle:
		if !reliable {
			return nil
		}
		return s.mountGrant(p, s.mountByIndex(1, m[2]), m[3], now, out)
	case game.RequestEnterMech:
		return s.mountGrant(p, s.mountByIndex(2, m[2]), 0, now, out)
	case game.RequestEnterMachinegun:
		id := binary.BigEndian.Uint32(m[2:])
		for _, v := range s.mounts {
			if v.definition.Category == 3 && v.definition.ObjectID == id {
				return s.mountGrant(p, v, 0, now, out)
			}
		}
		return nil
	case game.ExitVehicle, game.ExitMachinegun, game.ExitMech:
		if p.mount == nil {
			return nil
		}
		category := p.mount.vehicle.definition.Category
		if m[0] == game.ExitVehicle && category != 1 || m[0] == game.ExitMech && category != 2 || m[0] == game.ExitMachinegun && category != 3 {
			return nil
		}
		return s.leaveMount(p, now, out)
	}
	if p.mount == nil || p.life.dead || p.life.frozen {
		return nil
	}
	v := p.mount.vehicle
	index := m[1]
	if m[0] == game.VehicleKinematics {
		index = m[3]
	}
	if index != v.definition.WireIndex {
		return nil
	}
	var position world.Vec3
	var yaw, pitch uint16
	switch m[0] {
	case game.VehicleState:
		if v.definition.Category != 1 || p.mount.seat != 0 || len(m) != 19 {
			return nil
		}
		for _, offset := range []int{15} {
			f := math.Float32frombits(binary.LittleEndian.Uint32(m[offset:]))
			if math.IsNaN(float64(f)) || math.IsInf(float64(f), 0) {
				return nil
			}
		}
		encoded := [3]uint16{binary.BigEndian.Uint16(m[2:]), binary.BigEndian.Uint16(m[4:]), binary.BigEndian.Uint16(m[6:])}
		var err error
		position, err = s.movement.bounds.DecodePosition(encoded)
		if err != nil {
			return err
		}
		pitch, _ = pc.EncodeAngle(float64(binary.BigEndian.Uint16(m[8:])) * .01)
		yaw, _ = pc.EncodeAngle(float64(binary.BigEndian.Uint16(m[12:])) * .01)
		for axis := range v.rotation {
			v.rotation[axis] = float64(binary.BigEndian.Uint16(m[8+axis*2:])) * .01
		}
		v.state = append(v.state[:0], m...)
	case game.MechState:
		if v.definition.Category != 2 || len(m) != 10 {
			return nil
		}
		encoded := [3]uint16{binary.BigEndian.Uint16(m[2:]), binary.BigEndian.Uint16(m[4:]), binary.BigEndian.Uint16(m[6:])}
		var err error
		position, err = s.movement.bounds.DecodePosition(encoded)
		if err != nil {
			return err
		}
		pitch = uint16(m[8]) << 8
		yaw = uint16(m[9]) << 8
		if v.mech != nil {
			v.mech.Observe(collision.Vector{float32(position.X), float32(position.Y), float32(position.Z)}, float32(pc.DecodeAngle(yaw)), float32(pc.DecodeAngle(pitch)), s.elapsed(now))
		}
		v.state = append(v.state[:0], m...)
	case game.VehicleKinematics:
		if v.definition.Category != 1 || p.mount.seat != 0 || len(m) != 84 {
			return nil
		}
		for i := 0; i < 18; i++ {
			f := math.Float32frombits(binary.LittleEndian.Uint32(m[4+i*4:]))
			if math.IsNaN(float64(f)) || math.IsInf(float64(f), 0) {
				return nil
			}
		}
		// Only an occupied driver's state can update this native representation.
		// Position proposals have the same remaining physics-authority limitation
		// as on-foot movement; damage claims are never forwarded here.
		position = world.Vec3{X: float64(math.Float32frombits(binary.LittleEndian.Uint32(m[4:]))), Y: float64(math.Float32frombits(binary.LittleEndian.Uint32(m[8:]))), Z: float64(math.Float32frombits(binary.LittleEndian.Uint32(m[12:])))}
		if position.X < s.movement.bounds.Minimum.X || position.X > s.movement.bounds.Maximum.X || position.Y < s.movement.bounds.Minimum.Y || position.Y > s.movement.bounds.Maximum.Y || position.Z < s.movement.bounds.Minimum.Z || position.Z > s.movement.bounds.Maximum.Z {
			return nil
		}
		// SaveKinematicState writes padded position[4], quaternion[4], then
		// linear/angular velocities. Use that same orientation for target bodies.
		var q [4]float64
		var norm float64
		for i := range q {
			q[i] = float64(math.Float32frombits(binary.LittleEndian.Uint32(m[20+i*4:])))
			norm += q[i] * q[i]
		}
		if norm < .98 || norm > 1.02 {
			return nil
		}
		x, y, z, w := q[0], q[1], q[2], q[3]
		v.rotation = [3]float64{math.Atan2(2*(w*x+y*z), norm-2*(x*x+y*y)) * 180 / math.Pi, math.Asin(max(-1, min(1, 2*(w*y-z*x)/norm))) * 180 / math.Pi, math.Atan2(2*(w*z+x*y), norm-2*(y*y+z*z)) * 180 / math.Pi}
		v.kinematics = append(v.kinematics[:0], m...)
		clear(v.kinematics[76:84])
		m = v.kinematics
		pitch, _ = pc.EncodeAngle(v.rotation[0])
		yaw, _ = pc.EncodeAngle(v.rotation[2])
	case game.VehicleShooterState:
		if v.definition.Category != 1 || p.mount.seat != 1 {
			return nil
		}
		v.shooter = append(v.shooter[:0], m...)
		return s.presentMounted(p, m, reliable, now, out)
	default:
		return nil
	}
	if !v.lastPose.IsZero() && now.After(v.lastPose) {
		v.velocity = position.Sub(v.position).Mul(1 / now.Sub(v.lastPose).Seconds())
	}
	v.position, v.lastPose = position, now
	for _, occupant := range v.occupants {
		if occupant == nil {
			continue
		}
		pose := &s.movement.poses[occupant.player.Slot]
		pose.Position = [3]float32{float32(position.X), float32(position.Y), float32(position.Z)}
		pose.Time = s.elapsed(now)
		pose.Yaw = yaw
		pose.Pitch = pitch
	}
	return s.presentMounted(p, m, reliable, now, out)
}

func (s *Session) presentMounted(p *peer, message []byte, reliable bool, now time.Time, out *[]Delivery) error {
	body := []byte{game.NativeFrameTime, 0, 0, 0, 0}
	binary.BigEndian.PutUint32(body[1:], s.elapsed(now))
	body = append(body, message...)
	return s.present(p, body, reliable, now, out)
}

func (s *Session) mountedScene(p *peer, now time.Time) []byte {
	body := []byte{game.NativeFrameTime, 0, 0, 0, 0}
	// Activation is followed immediately by the current server simulation time.
	binary.BigEndian.PutUint32(body[1:], s.elapsed(now))
	for _, v := range s.mounts {
		if !v.respawnAt.IsZero() {
			body = append(body, 30, v.definition.StaticWireIndex)
			continue
		}
		body = append(body, v.kinematics...)
		body = append(body, v.state...)
		body = append(body, v.shooter...)
	}
	for _, other := range s.peers {
		if other != p && other.mount != nil && !other.departed {
			body = append(body, mountGrantMessage(other)...)
		}
	}
	if len(body) == 5 {
		return nil
	}
	return body
}
