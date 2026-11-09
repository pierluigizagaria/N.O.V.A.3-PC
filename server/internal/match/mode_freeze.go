package match

import (
	"encoding/binary"
	"time"
)

func (s *Session) freezeVictim(attacker, victim *peer, now time.Time, out *[]Delivery) error {
	if err := s.leaveMount(victim, now, out); err != nil {
		return err
	}
	victim.life.frozen = true
	victim.life.freezeStreak = 0
	victim.life.thawStreak = 0
	clear(victim.life.rescueSince[:])
	if attacker != victim {
		attacker.life.freezeStreak++
		key := "freeze_streak"
		if attacker.life.freezeStreak == 1 {
			key = "freeze_first"
		}
		s.objectiveXP(attacker, key, attacker.life.freezeStreak)
	}
	for _, w := range victim.weapons {
		w.ammo.CancelReload(now)
	}
	return s.modeBroadcast([]byte{43, victim.player.Slot, attacker.player.Slot, 1}, now, out)
}
func (s *Session) advanceFrozen(now time.Time, out *[]Delivery) error {
	o := s.objectives
	r := o.data.Rules
	var counts, frozen [2]int
	for _, victim := range s.peers {
		if !victim.sceneReady || victim.departed || victim.transportFailed {
			continue
		}
		team := victim.player.Beacon[0] - 1
		counts[team]++
		if !victim.life.frozen {
			continue
		}
		frozen[team]++
		for _, helper := range s.peers {
			if helper == victim {
				continue
			}
			i := helper.player.Slot
			start := victim.life.rescueSince[i]
			d := s.actorPosition(helper).Sub(s.actorPosition(victim))
			near := s.objectiveActor(helper) && s.teammates(helper, victim) && d.Dot(d) < r.RescueDistanceSquared
			if near && start.IsZero() {
				victim.life.rescueSince[i] = now
				body := binary.BigEndian.AppendUint32([]byte{48, victim.player.Slot, i}, s.elapsed(now))
				body = append(body, 1)
				if err := s.modeBroadcast(body, now, out); err != nil {
					return err
				}
			} else if !near && !start.IsZero() {
				victim.life.rescueSince[i] = time.Time{}
				body := binary.BigEndian.AppendUint32([]byte{48, victim.player.Slot, i}, s.elapsed(start))
				body = append(body, 0)
				if err := s.modeBroadcast(body, now, out); err != nil {
					return err
				}
			} else if near && !start.IsZero() && now.Sub(start) >= time.Duration(r.RescueMS)*time.Millisecond {
				helper.life.thawStreak++
				key := "unfreeze_streak"
				if helper.life.thawStreak == 1 {
					key = "unfreeze_first"
				}
				s.objectiveXP(helper, key, helper.life.thawStreak)
				if err := s.modeBroadcast([]byte{43, victim.player.Slot, i, 0}, now, out); err != nil {
					return err
				}
				victim.roundSpawnPending = true
				if err := s.spawnRound(victim, now, out); err != nil {
					return err
				}
				frozen[team]--
				break
			}
		}
	}
	winner := byte(0)
	if counts[0] > 0 && frozen[0] == counts[0] {
		winner = 2
	} else if counts[1] > 0 && frozen[1] == counts[1] {
		winner = 1
	}
	if winner == 0 {
		o.freezeUntil = time.Time{}
		o.freezeWinner = 0
		return nil
	}
	if o.freezeWinner != winner || o.freezeUntil.IsZero() {
		o.freezeWinner = winner
		o.freezeUntil = now.Add(time.Duration(r.FreezeRoundMS) * time.Millisecond)
		return nil
	}
	if now.Before(o.freezeUntil) {
		return nil
	}
	s.teamScores[winner-1]++
	if err := s.modeBroadcast([]byte{45, winner}, now, out); err != nil {
		return err
	}
	if err := s.objectiveLimit(now, out); err != nil || s.phase != Playing {
		return err
	}
	return s.resetObjectiveRound(now, out)
}
