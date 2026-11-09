package match

import (
	"time"

	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/world"
)

// Keep exact posed collision volumes from the dedicated simulation. A query
// selects a completed simulation frame; it never warps a live actor, blends
// capsule dimensions or rewinds health/score. The bound covers the existing
// two-second shot admission window plus presentation and scheduling margins.
const hitHistoryCapacity = 128

type historicalPlayer struct {
	generation    uint32
	bornAt        uint32
	poseTime      uint32
	position      world.Vec3
	yaw, pitch    uint16
	mountCategory byte
	regions       []world.HitRegion
}
type historicalMount struct {
	generation uint32
	position   world.Vec3
	regions    []world.HitRegion
}
type hitFrame struct {
	time    uint32
	players map[*peer]historicalPlayer
	mounts  map[*mountState]historicalMount
}

func (s *Session) recordHitFrame(now time.Time) {
	if s.phase != Playing || s.epoch.IsZero() || s.movement == nil || s.authority == nil {
		return
	}
	stamp := s.elapsed(now)
	if n := len(s.hitHistory); n > 0 && stamp != s.hitHistory[n-1].time && !pc.Newer(stamp, s.hitHistory[n-1].time) {
		return
	}
	regions := s.targetRegions(now)
	f := hitFrame{time: stamp, players: make(map[*peer]historicalPlayer, len(regions)), mounts: make(map[*mountState]historicalMount, len(s.mounts))}
	for _, p := range s.peers {
		if p.departed || p.life.dead || !p.sceneReady || p.roundSpawnPending {
			continue
		}
		pose := s.movement.poses[p.player.Slot]
		entry := historicalPlayer{generation: pose.Generation, bornAt: p.life.bornAt, poseTime: pose.Time,
			position: world.Vec3{X: float64(pose.Position[0]), Y: float64(pose.Position[1]), Z: float64(pose.Position[2])},
			yaw:      pose.Yaw, pitch: pose.Pitch, regions: regions[p]}
		if p.mount != nil {
			entry.mountCategory = p.mount.vehicle.definition.Category
			if entry.mountCategory == 1 || entry.mountCategory == 2 {
				entry.poseTime = stamp
			}
			if entry.mountCategory == 1 && p.mount.vehicle.definition.Occupants != nil {
				entry.position = p.mount.vehicle.occupantFrame(p.mount.seat).position
			} else if entry.mountCategory == 3 && p.mount.vehicle.definition.Gunner != nil {
				position, _, _, _ := fixedGunnerPose(p.mount.vehicle.definition.Gunner, pose)
				entry.position = world.Vec3{X: float64(position[0]), Y: float64(position[1]), Z: float64(position[2])}
			}
		}
		// PC presentation uses the accepted source pose clock. Tagging those
		// regions only with receipt time would subtract uplink latency twice.
		// Native car/mech replicas instead use their server-receipt state clock.
		if pc.Newer(entry.poseTime, stamp) {
			entry.poseTime = stamp
		}
		if pc.Newer(entry.bornAt, entry.poseTime) {
			entry.poseTime = entry.bornAt
		}
		f.players[p] = entry
	}
	for _, v := range s.mounts {
		f.mounts[v] = historicalMount{generation: v.generation, position: v.position, regions: v.regions()}
	}
	if n := len(s.hitHistory); n > 0 && s.hitHistory[n-1].time == stamp {
		s.hitHistory[n-1] = f
	} else if len(s.hitHistory) == hitHistoryCapacity {
		copy(s.hitHistory, s.hitHistory[1:])
		s.hitHistory[len(s.hitHistory)-1] = f
	} else {
		s.hitHistory = append(s.hitHistory, f)
	}
}

// The dedicated server bounds timestamp compensation by its own measured RTT
// plus the existing two30Hz-frame budget. The native two-second packet-age
// envelope remains an admission limit, not an arbitrary client rewind grant.
func (s *Session) confirmationTime(p *peer, claimed uint32, now time.Time) uint32 {
	current := s.elapsed(now)
	measured := min(max(p.ping.rtt, p.clock.bestRTT), uint32(2000))
	budget := min(measured+pc.PresentationDelayMS, uint32(2000))
	if pc.Newer(claimed, current) {
		return current
	}
	if age := current - claimed; age > budget {
		return current - budget
	}
	return claimed
}

func presentationDelay(category byte) uint32 {
	switch category {
	case 1:
		return 0 // Car transforms still use their native vehicle-state path.
	case 2:
		return 134 // Same two-native-period mech history as the installed client.
	default:
		return pc.PresentationDelayMS
	}
}

func (s *Session) historicalPlayer(p *peer, stamp uint32) (historicalPlayer, bool) {
	for i := len(s.hitHistory) - 1; i >= 0; i-- {
		f := &s.hitHistory[i]
		v, ok := f.players[p]
		if !ok || v.bornAt != p.life.bornAt || v.generation != s.movement.poses[p.player.Slot].Generation || pc.Newer(v.poseTime, stamp) {
			continue
		}
		return v, true
	}
	return historicalPlayer{}, false
}

func (s *Session) historicalMount(v *mountState, stamp uint32) (historicalMount, bool) {
	for i := len(s.hitHistory) - 1; i >= 0; i-- {
		f := &s.hitHistory[i]
		if pc.Newer(f.time, stamp) {
			continue
		}
		h, ok := f.mounts[v]
		return h, ok && h.generation == v.generation
	}
	return historicalMount{}, false
}

func (s *Session) displayedPlayer(p *peer, fired, stamp uint32) (historicalPlayer, bool) {
	if v, ok := s.historicalPlayer(p, stamp); ok {
		return v, true
	}
	if int32(stamp-p.life.bornAt) < 0 && int32(fired-p.life.bornAt) >= 0 {
		for _, f := range s.hitHistory {
			if v, ok := f.players[p]; ok && v.bornAt == p.life.bornAt && v.generation == s.movement.poses[p.player.Slot].Generation {
				return v, true
			}
		}
	}
	return historicalPlayer{}, false
}

func (s *Session) shotTargets(b bullet, now time.Time) (map[*peer][]world.HitRegion, map[*mountState][]world.HitRegion) {
	players := make(map[*peer][]world.HitRegion, len(s.peers))
	mounts := make(map[*mountState][]world.HitRegion, len(s.mounts))
	// Physical projectiles keep current-world collision. Bullet/ray confirmation
	// refers to the shooter's displayed timeline; elapsed flight
	// advances that reference instead of freezing targets at discharge time.
	compensated := b.projectile.Ray || b.projectile.Railgun || b.projectile.Bullet && !b.projectile.DamagePerSecond
	if !compensated || !b.haveFireTime {
		players = s.targetRegions(now)
		for _, v := range s.mounts {
			mounts[v] = v.regions()
		}
		return players, mounts
	}
	stamp := b.fireTime + uint32(max(now.Sub(b.started).Milliseconds(), 0))
	for _, p := range s.peers {
		if p.departed || p.life.dead || !p.sceneReady || p.roundSpawnPending {
			continue
		}
		category := byte(0)
		if p.mount != nil {
			category = p.mount.vehicle.definition.Category
		}
		at := stamp - presentationDelay(category)
		if v, ok := s.displayedPlayer(p, stamp, at); ok {
			players[p] = v.regions
		}
	}
	for _, v := range s.mounts {
		at := stamp - presentationDelay(v.definition.Category)
		if h, ok := s.historicalMount(v, at); ok {
			mounts[v] = h.regions
		}
	}
	return players, mounts
}
