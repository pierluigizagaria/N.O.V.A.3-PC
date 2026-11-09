package match

import (
	"encoding/binary"
	"math"
	"time"

	"nova3.test/backend/internal/world"
)

type flagState struct {
	definition objectiveDefinition
	position   world.Vec3
	zone       int32
	carrier    *peer
}
type pointState struct {
	definition       objectiveDefinition
	progress         float64
	owner, lastOwner byte
	direction        int8
	members          map[*peer]bool
}
type objectiveState struct {
	data                              *objectiveDocument
	flags                             [2]*flagState
	bases                             [2]objectiveDefinition
	points                            []*pointState
	lastTick, nextSnapshot, holdUntil time.Time
	holding                           byte
	freezeWinner                      byte
	freezeUntil                       time.Time
}

func (s *Session) initObjectives() error {
	data, err := loadModeData()
	if err != nil {
		return err
	}
	o := &objectiveState{data: data}
	m := data.Maps[s.loading.Map]
	if s.loading.Mode == CaptureTheFlag {
		for _, d := range m.Flags {
			if d.Enabled && d.Team >= 1 && d.Team <= 2 {
				o.flags[d.Team-1] = &flagState{definition: d, position: d.position(), zone: d.Zone}
			}
		}
		for _, d := range m.Bases {
			if d.Enabled && d.Team >= 1 && d.Team <= 2 {
				o.bases[d.Team-1] = d
			}
		}
		for i := range o.flags {
			if o.flags[i] == nil || o.bases[i].Object == 0 {
				return ErrState
			}
		}
	}
	if s.loading.Mode == CaptureThePoint {
		for _, d := range m.Points {
			if d.Enabled {
				p := &pointState{definition: d, members: make(map[*peer]bool)}
				p.reset(data.Rules.PointMaximum)
				o.points = append(o.points, p)
			}
		}
		if len(o.points) == 0 {
			return ErrState
		}
	}
	s.objectives = o
	return nil
}
func (p *pointState) reset(maximum float64) {
	p.owner, p.lastOwner, p.direction = 0, 0, 0
	p.progress = maximum / 2
	clear(p.members)
}
func (s *Session) modeBroadcast(body []byte, now time.Time, out *[]Delivery) error {
	for _, p := range s.peers {
		if p.sceneReady && !p.departed {
			if err := s.emit(p, body, true, now, out); err != nil {
				return err
			}
		}
	}
	return nil
}
func (s *Session) objectiveActor(p *peer) bool {
	return p.sceneReady && !p.departed && !p.transportFailed && !p.life.dead && !p.life.frozen && !p.roundSpawnPending
}
func (s *Session) actorPosition(p *peer) world.Vec3 {
	v := s.movement.poses[p.player.Slot].Position
	return world.Vec3{X: float64(v[0]), Y: float64(v[1]), Z: float64(v[2])}
}
func (s *Session) objectiveXP(p *peer, key string, multiple uint32) {
	value := uint64(s.objectives.data.Rewards[key]) * uint64(multiple)
	base := uint32(min(value, 2147483647))
	boosted := s.consumableXP(p, base)
	p.life.xpBonus = uint32(min(uint64(p.life.xpBonus)+uint64(boosted-base), 2147483647))
	p.life.score = uint32(min(uint64(p.life.score)+value, 2147483647))
}

func (s *Session) flagMessage(f *flagState, actor byte, attached, returned bool) ([]byte, error) {
	position, err := s.movement.bounds.EncodePosition(f.position)
	if err != nil {
		return nil, err
	}
	b := []byte{41, 0, 0, 0, 0, actor, 255, 0}
	binary.BigEndian.PutUint32(b[1:], f.definition.Object)
	if attached {
		b[7] = 1
	}
	for _, x := range position {
		b = binary.BigEndian.AppendUint16(b, x)
	}
	b = binary.BigEndian.AppendUint32(b, uint32(f.zone))
	if returned {
		b = append(b, 1)
	} else {
		b = append(b, 0)
	}
	return b, nil
}
func (s *Session) returnFlag(f *flagState, actor byte, attached bool, now time.Time, out *[]Delivery) error {
	f.carrier = nil
	f.position = f.definition.position()
	f.zone = f.definition.Zone
	b, err := s.flagMessage(f, actor, attached, true)
	if err != nil {
		return err
	}
	return s.modeBroadcast(b, now, out)
}
func (s *Session) dropFlag(p *peer, now time.Time, out *[]Delivery) error {
	if s.objectives == nil || s.loading.Mode != CaptureTheFlag {
		return nil
	}
	for _, f := range s.objectives.flags {
		if f.carrier == p {
			f.carrier = nil
			f.position = s.actorPosition(p)
			if p.zone != nil {
				f.zone = int32(p.zone.Zone)
			}
			b, err := s.flagMessage(f, p.player.Slot, false, false)
			if err != nil {
				return err
			}
			if err = s.modeBroadcast(b, now, out); err != nil {
				return err
			}
		}
	}
	return nil
}
func (s *Session) advanceFlags(now time.Time, out *[]Delivery) error {
	o := s.objectives
	for _, f := range o.flags {
		if f.carrier != nil {
			p := f.carrier
			if !s.objectiveActor(p) || p.mount != nil {
				if err := s.dropFlag(p, now, out); err != nil {
					return err
				}
				continue
			}
			f.position = s.actorPosition(p)
			base := o.bases[p.player.Beacon[0]-1]
			own := o.flags[p.player.Beacon[0]-1]
			radius := base.Radius + float64(s.authority.Player().Body.Radius)
			delta := f.position.Sub(base.position())
			home := own.position.Sub(own.definition.position())
			if p.mount == nil && delta.Dot(delta) < radius*radius && own.carrier == nil && math.Sqrt(home.Dot(home)) < o.data.Rules.FlagHomeTolerance {
				if err := s.returnFlag(f, p.player.Slot, true, now, out); err != nil {
					return err
				}
				team := p.player.Beacon[0]
				s.teamScores[team-1]++
				p.life.flags++
				s.objectiveXP(p, "flag_capture", 1)
				if err := s.modeBroadcast([]byte{42, team, p.player.Slot}, now, out); err != nil {
					return err
				}
				return s.objectiveLimit(now, out)
			}
			continue
		}
		for _, p := range s.peers {
			if !s.objectiveActor(p) || p.mount != nil {
				continue
			}
			radius := f.definition.Radius + float64(s.authority.Player().Body.Radius)
			delta := s.actorPosition(p).Sub(f.position)
			if delta.Dot(delta) >= radius*radius {
				continue
			}
			if p.player.Beacon[0] == f.definition.Team {
				home := f.position.Sub(f.definition.position())
				if math.Sqrt(home.Dot(home)) > o.data.Rules.FlagReturnTolerance {
					if err := s.returnFlag(f, p.player.Slot, false, now, out); err != nil {
						return err
					}
					s.objectiveXP(p, "flag_return", 1)
				}
			} else {
				carrying := false
				for _, other := range o.flags {
					carrying = carrying || other.carrier == p
				}
				if carrying {
					continue
				}
				f.carrier = p
				s.objectiveXP(p, "flag_pick", 1)
				b, err := s.flagMessage(f, p.player.Slot, true, false)
				if err != nil {
					return err
				}
				if err = s.modeBroadcast(b, now, out); err != nil {
					return err
				}
			}
			break
		}
	}
	return nil
}
func (s *Session) pointOwner(p *pointState) byte {
	if p.progress < p.definition.Threshold {
		return 1
	}
	if p.progress > s.objectives.data.Rules.PointMaximum-p.definition.Threshold {
		return 2
	}
	return 0
}
func (s *Session) pointSnapshot(index int, p *pointState) []byte {
	b := []byte{123, byte(index), 0, 0, 0, 0, byte(p.direction + 1), 255}
	binary.BigEndian.PutUint32(b[2:], uint32(p.progress*s.objectives.data.Rules.PointScale))
	for _, peer := range s.peers {
		if p.members[peer] {
			b[7] = peer.player.Slot
			break
		}
	}
	return b
}
func (s *Session) advancePoints(now time.Time, delta time.Duration, out *[]Delivery) error {
	o := s.objectives
	for index, p := range o.points {
		var counts [2]int
		for _, actor := range s.peers {
			v := s.actorPosition(actor).Sub(p.definition.position())
			inside := s.objectiveActor(actor) && !now.Before(actor.life.pointReadyAt) && v.X*v.X+v.Y*v.Y < p.definition.Radius*p.definition.Radius
			if inside != p.members[actor] {
				message := []byte{121, actor.player.Slot, byte(index)}
				if inside {
					message[0] = 120
					p.members[actor] = true
				} else {
					delete(p.members, actor)
				}
				if err := s.modeBroadcast(message, now, out); err != nil {
					return err
				}
			}
			if inside {
				counts[actor.player.Beacon[0]-1]++
			}
		}
		p.direction = 0
		if counts[0] > counts[1] {
			p.direction = -1
		}
		if counts[1] > counts[0] {
			p.direction = 1
		}
		previous := p.owner
		p.progress = max(0, min(o.data.Rules.PointMaximum, p.progress+float64(p.direction)*p.definition.Rate*delta.Seconds()))
		p.owner = s.pointOwner(p)
		if previous != p.owner {
			b := []byte{122, byte(index), 0, 0, 0, 0, p.owner, previous}
			binary.BigEndian.PutUint32(b[2:], math.Float32bits(float32(p.progress)))
			if err := s.modeBroadcast(b, now, out); err != nil {
				return err
			}
			if p.owner != 0 {
				key := "point_capture"
				if p.lastOwner != 0 && p.lastOwner != p.owner {
					key = "point_enemy_capture"
				}
				for actor := range p.members {
					if actor.player.Beacon[0] == p.owner {
						s.objectiveXP(actor, key, 1)
					}
				}
				p.lastOwner = p.owner
			}
		}
	}
	team := o.points[0].owner
	for _, p := range o.points {
		if p.owner != team {
			team = 0
		}
	}
	if team == 0 {
		o.holdUntil = time.Time{}
		o.holding = 0
	} else if o.holding != team || o.holdUntil.IsZero() {
		o.holding = team
		o.holdUntil = now.Add(time.Duration(o.data.Rules.PointHoldMS) * time.Millisecond)
	} else if !now.Before(o.holdUntil) {
		s.teamScores[team-1]++
		for _, actor := range s.peers {
			if s.objectiveActor(actor) && actor.player.Beacon[0] == team {
				s.objectiveXP(actor, "point_score", 1)
			}
		}
		if err := s.modeBroadcast([]byte{124, team}, now, out); err != nil {
			return err
		}
		if err := s.objectiveLimit(now, out); err != nil || s.phase != Playing {
			return err
		}
		return s.resetObjectiveRound(now, out)
	}
	if !now.Before(o.nextSnapshot) {
		o.nextSnapshot = now.Add(time.Second)
		for i, p := range o.points {
			if err := s.modeBroadcast(s.pointSnapshot(i, p), now, out); err != nil {
				return err
			}
		}
		remaining := uint32(0xffffffff)
		if !o.holdUntil.IsZero() {
			remaining = uint32(max(0, o.holdUntil.Sub(now)/time.Millisecond))
		}
		b := binary.BigEndian.AppendUint32([]byte{125}, remaining)
		b = append(b, o.holding)
		if err := s.modeBroadcast(b, now, out); err != nil {
			return err
		}
	}
	return nil
}
func (s *Session) objectiveLimit(now time.Time, out *[]Delivery) error {
	if s.loading.Parameter2 != 0 && (s.teamScores[0] >= int32(s.loading.Parameter2) || s.teamScores[1] >= int32(s.loading.Parameter2)) {
		v, err := s.finishOrOvertime(now)
		*out = append(*out, v...)
		return err
	}
	return nil
}
func (s *Session) resetObjectiveRound(now time.Time, out *[]Delivery) error {
	o := s.objectives
	o.holdUntil = time.Time{}
	o.holding = 0
	o.freezeUntil = time.Time{}
	o.freezeWinner = 0
	for _, p := range o.points {
		p.reset(o.data.Rules.PointMaximum)
	}
	for _, p := range s.peers {
		if p.sceneReady && !p.departed {
			if err := s.leaveMount(p, now, out); err != nil {
				return err
			}
		}
	}
	if err := s.resetMounts(now, out); err != nil {
		return err
	}
	if err := s.resetPickups(now, out); err != nil {
		return err
	}
	for _, p := range s.peers {
		if p.sceneReady && !p.departed {
			p.roundSpawnPending = true
			if err := s.spawnRound(p, now, out); err != nil {
				return err
			}
		}
	}
	return nil
}
func (s *Session) advanceObjectives(now time.Time, out *[]Delivery) error {
	if s.objectives == nil {
		return nil
	}
	o := s.objectives
	delta := now.Sub(o.lastTick)
	o.lastTick = now
	if s.phase != Playing || s.round.runningSince.IsZero() || delta < 0 {
		o.holdUntil, o.freezeUntil = time.Time{}, time.Time{}
		o.holding, o.freezeWinner = 0, 0
		return nil
	}
	switch s.loading.Mode {
	case FreezeTag:
		return s.advanceFrozen(now, out)
	case CaptureTheFlag:
		return s.advanceFlags(now, out)
	case CaptureThePoint:
		return s.advancePoints(now, delta, out)
	}
	return nil
}

func (s *Session) objectiveScene() ([]byte, error) {
	if s.objectives == nil {
		return nil, nil
	}
	var body []byte
	if s.loading.Mode == CaptureTheFlag {
		for _, f := range s.objectives.flags {
			// The initial snapshot already places unattached flags. Native41
			// dereferences the actor's team even when IsPlayerActive is false;
			// an absent carrier must never be replayed as actor255.
			if f.carrier == nil {
				continue
			}
			message, err := s.flagMessage(f, f.carrier.player.Slot, true, false)
			if err != nil {
				return nil, err
			}
			body = append(body, message...)
		}
	}
	if s.loading.Mode == CaptureThePoint {
		for i, p := range s.objectives.points {
			body = append(body, s.pointSnapshot(i, p)...)
		}
	}
	if s.loading.Mode == FreezeTag {
		for _, p := range s.peers {
			if p.sceneReady && p.life.frozen {
				body = append(body, 43, p.player.Slot, p.player.Slot, 1)
			}
		}
	}
	return body, nil
}
