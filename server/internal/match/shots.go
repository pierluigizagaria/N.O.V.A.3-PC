package match

import (
	"math"
	"math/rand/v2"
	"time"

	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/world"
)

// authorizeShot spends ammunition only after ownership, clock, origin, direction
// and the server-time firing envelope have passed. It never accepts a client hit.
func (s *Session) authorizeShot(p *peer, v game.Shot, now time.Time) (world.Vec3, world.Vec3, *weaponState, bool) {
	p.shotIntents++
	p.shotDiagnostic = v
	p.shotReject = 1
	if s.authority == nil || s.movement == nil || s.movement.scene == nil || !p.sceneReady || p.departed || p.roundSpawnPending || p.life.dead || p.life.frozen || s.round.runningSince.IsZero() {
		return world.Vec3{}, world.Vec3{}, nil, false
	}
	if p.mount != nil {
		return s.authorizeMountedShot(p, v, now)
	}
	w := p.weapons[p.weapon]
	p.shotReject = 2
	if w == nil || v.Weapon != w.definition.WireIndex || v.SourceType != 3 || v.Flags&1 != 0 {
		return world.Vec3{}, world.Vec3{}, nil, false
	}
	p.shotReject = 3
	if _, ok := s.authority.Projectile(w.definition.FireModes[0].ProjectileTypeID); !ok {
		return world.Vec3{}, world.Vec3{}, nil, false // Grenades have their own intent/lifecycle.
	}
	delta := int32(v.Time - s.elapsed(now))
	p.shotReject = 4
	if delta < -2000 || delta > 2000 || int32(v.Time-p.life.bornAt) < 0 || (w.haveShot && int32(v.Time-w.lastShot) <= 0) {
		return world.Vec3{}, world.Vec3{}, nil, false
	}
	origin, err := s.movement.bounds.DecodePosition(v.Origin)
	p.shotReject = 5
	if err != nil {
		return world.Vec3{}, world.Vec3{}, nil, false
	}
	direction, err := world.DecodeShotDirection(v.Direction)
	p.shotReject = 6
	if err != nil {
		return world.Vec3{}, world.Vec3{}, nil, false
	}
	pose := s.movement.poses[p.player.Slot].Position
	if past, ok := s.historicalPlayer(p, s.confirmationTime(p, v.Time, now)); ok && past.mountCategory == 0 {
		pose = [3]float32{float32(past.position.X), float32(past.position.Y), float32(past.position.Z)}
	}
	body := s.authority.Player().Body
	center := world.Vec3{X: float64(pose[0] + body.OffsetX), Y: float64(pose[1] + body.OffsetY), Z: float64(pose[2] + body.OffsetZ + body.Height*.5)}
	// The actor body bounds the proposed muzzle, never the projectile target.
	// Include one native quantization cell so map scale cannot reject an edge.
	cell := s.movement.bounds.Maximum.Sub(s.movement.bounds.Minimum).Mul(1. / 65535)
	limit := float64(body.Height*.5+body.Radius) + math.Sqrt(cell.Dot(cell))
	offset := origin.Sub(center)
	p.shotReject = 7
	if offset.Dot(offset) > limit*limit {
		return world.Vec3{}, world.Vec3{}, nil, false
	}
	p.shotReject = 8
	if _, blocked, err := s.movement.scene.Raycast(center, origin, world.Filter{Group: 0x401, Mask: 0x76f7}, nil); err != nil || blocked {
		return world.Vec3{}, world.Vec3{}, nil, false
	}
	interval, err := w.definition.FireInterval(0, w.modifiers)
	p.shotReject = 9
	if err != nil || !w.fire.Allows(now, interval) || !s.burstAllows(w, 0, now) || !w.ammo.Consume(now) {
		return world.Vec3{}, world.Vec3{}, nil, false
	}
	w.fire.Commit(now, interval)
	s.commitBurst(w, 0, now)
	w.lastShot, w.haveShot = v.Time, true
	p.life.shots++
	p.shotReject = 0
	return origin, direction, w, true
}

func (s *Session) shoot(p *peer, message []byte, now time.Time, out *[]Delivery) error {
	v, _ := game.ReadShot(message)
	origin, direction, w, ok := s.authorizeShot(p, v, now)
	if !ok {
		return nil
	}
	// A validated discharge is presented remotely. Projectile simulation owns
	// hit/damage notifications separately; this message contains no trusted hit.
	if err := s.present(p, message, false, now, out); err != nil {
		return err
	}
	projectile, _ := s.authority.Projectile(w.definition.FireModes[int(v.Flags&1)].ProjectileTypeID)
	if len(s.hitHistory) == 0 {
		s.recordHitFrame(now)
	}
	if s.collider(p, now) != nil && projectile.Speed > 0 && len(s.bullets)+int(w.definition.ProjectilesPerShot) <= 2048 {
		action := 4
		if p.aiming {
			action = 5
		}
		s.collisionAction(p, action, now)
		discharge := &dischargeHit{}
		p.lastDischarge = discharge
		var sourceObject uint32
		if p.mount != nil {
			sourceObject = p.mount.vehicle.definition.ObjectID
		}
		for pellet := int32(0); pellet < w.definition.ProjectilesPerShot; pellet++ {
			spread := direction
			if w.definition.ProjectilesPerShot > 1 && !w.definition.SpreadSuppressed {
				spread = pelletDirection(direction, float64(v.Scalar)/3276.75, int(pellet), int(w.definition.ProjectilesPerShot), w.definition.SpreadPattern)
			}
			b := bullet{owner: p, bornAt: p.life.bornAt, weapon: w, projectile: projectile, position: origin, direction: spread, last: now, discharge: discharge, sourceObject: sourceObject, fireTime: s.confirmationTime(p, v.Time, now), started: now, haveFireTime: true}
			if projectile.DamagePerSecond || projectile.Railgun {
				lifetime := projectile.LifetimeMS
				if projectile.Railgun {
					lifetime = projectile.ActiveMS
				}
				b.expires = now.Add(time.Duration(lifetime) * time.Millisecond)
				b.tail = origin.Sub(spread.Mul(float64(projectile.RayLength)))
			}
			s.bullets = append(s.bullets, b)
		}
	}
	return nil
}

// Native GetSpread uses a central pellet plus a ring (pattern1), or a horizontal
// sinusoid (pattern2), followed by independent integer jitter on both axes.
// Only the multi-projectile message carries an unspread direction and angle;
// a single-projectile message already contains its resulting direction.
func pelletDirection(d world.Vec3, angle float64, index, count int, pattern int32) world.Vec3 {
	right := world.Vec3{X: d.Y, Y: -d.X}
	if length := math.Sqrt(right.Dot(right)); length > 0 {
		right = right.Mul(1 / length)
	}
	up := world.Vec3{X: right.Y * d.Z, Y: -right.X * d.Z, Z: right.X*d.Y - right.Y*d.X}
	var yaw, pitch float64
	if pattern == 1 && index > 0 {
		phase := float64(index-1) * 2 * math.Pi / float64(count-1)
		yaw, pitch = math.Cos(phase)*angle, math.Sin(phase)*angle
	} else if pattern == 2 {
		pitch = math.Cos(float64(index)*2*math.Pi/float64(count)) * max(1, angle)
	}
	yaw += -angle + float64(rand.IntN(100))*angle*.02
	pitch += -angle + float64(rand.IntN(100))*angle*.02
	rotate := func(v, axis world.Vec3, degrees float64) world.Vec3 {
		sine, cosine := math.Sincos(degrees * math.Pi / 180)
		cross := world.Vec3{X: axis.Y*v.Z - axis.Z*v.Y, Y: axis.Z*v.X - axis.X*v.Z, Z: axis.X*v.Y - axis.Y*v.X}
		return v.Mul(cosine).Add(cross.Mul(sine)).Add(axis.Mul(axis.Dot(v) * (1 - cosine)))
	}
	d = rotate(rotate(d, up, yaw), right, pitch)
	return d.Mul(1 / math.Sqrt(d.Dot(d)))
}
