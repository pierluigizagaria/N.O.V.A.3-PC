package match

import (
	"math"
	"time"

	"nova3.test/backend/internal/protocol/game"
)

type lifeState struct {
	xpBonus                         uint32
	bornAt                          uint32
	health                          float32
	dead                            bool
	frozen                          bool
	pointReadyAt                    time.Time
	flags, freezeStreak, thawStreak uint32
	rescueSince                     [12]time.Time
	kills, deaths, score            uint32
	shots, hits                     uint32
	headshots                       uint32
	participated                    bool
	playedFrom, playedTo            uint32 // Shared active-round clock, milliseconds.
	playStopped                     bool
	streak                          uint32
	victims                         map[string]uint32
}

func (s *Session) resetLife(p *peer, now time.Time) {
	if !s.round.runningSince.IsZero() && !p.life.participated {
		p.life.participated = true
		p.life.playedFrom = s.roundClock(now).Elapsed
	}
	if s.authority != nil {
		p.life.health = s.authority.Player().BaseHealth
	}
	p.life.dead = false
	p.life.frozen = false
	if s.objectives != nil {
		p.life.pointReadyAt = now.Add(time.Duration(s.objectives.data.Rules.PointReadyMS) * time.Millisecond)
	}
	clear(p.life.rescueSince[:])
	p.chemicalUntil = time.Time{}
	p.nextMelee = time.Time{}
	p.collider = nil
	p.life.bornAt = s.elapsed(now)
}

// damage is an internal simulation result, never a client-supplied hit or
// amount. Quantize once and apply exactly the damage sent to the native victim.
func (s *Session) damage(attacker, victim *peer, weapon uint16, amount float32, now time.Time, out *[]Delivery) error {
	return s.damageImpact(attacker, victim, weapon, amount, 0, now, out)
}

func (s *Session) damageImpact(attacker, victim *peer, weapon uint16, amount float32, partID byte, now time.Time, out *[]Delivery) error {
	return s.damageKind(attacker, victim, weapon, amount, partID, 0, 0, now, out)
}

func (s *Session) damageKind(attacker, victim *peer, weapon uint16, amount float32, partID, kind, source byte, now time.Time, out *[]Delivery) error {
	if !s.canDamage(attacker, victim) || victim.life.frozen {
		return nil
	}
	if s.authority == nil || s.phase != Playing || s.round.runningSince.IsZero() || victim.life.dead || victim.roundSpawnPending || !victim.sceneReady || victim.departed || amount <= 0 || math.IsNaN(float64(amount)) || math.IsInf(float64(amount), 0) {
		return nil
	}
	if now.Before(victim.consumables.until[8]) {
		return nil
	}
	amount = min(amount, victim.life.health)
	var body []byte
	for amount >= 1./32 {
		part := min(amount, 255)
		d := game.Damage{Attacker: attacker.player.Slot, Victim: victim.player.Slot, Kind: kind}
		if partID == 3 {
			d.Flags |= 2
		}
		if part < 8 {
			d.Flags |= 1
			d.Amount = byte(part * 32)
		} else {
			d.Amount = byte(part)
		}
		value := d.Value()
		if value == 0 {
			break
		}
		m, _ := d.Message()
		body = append(body, m...)
		amount -= value
		victim.life.health -= value
	}
	if len(body) == 0 {
		return nil
	}
	if err := s.emit(victim, body, true, now, out); err != nil {
		return err
	}
	hit, _ := (game.Hit{Attacker: attacker.player.Slot, Victim: victim.player.Slot, Part: partID}).Message()
	for _, p := range s.peers {
		if p != victim && p.sceneReady && !p.departed {
			if err := s.emit(p, hit, true, now, out); err != nil {
				return err
			}
		}
	}
	if victim.life.health >= 1./32 {
		return nil
	}
	victim.life.health = 0
	if s.loading.Mode == FreezeTag {
		return s.freezeVictim(attacker, victim, now, out)
	}
	if err := s.dropFlag(victim, now, out); err != nil {
		return err
	}
	if err := s.dropWeapon(victim, now, out); err != nil {
		return err
	}
	if err := s.leaveMount(victim, now, out); err != nil {
		return err
	}
	victim.life.dead = true
	victim.life.deaths++
	victim.life.streak = 0
	if attacker != victim && !s.teammates(attacker, victim) {
		attacker.life.kills++
		if partID == 3 {
			attacker.life.headshots++
		}
		attacker.life.streak++
		if attacker.life.victims == nil {
			attacker.life.victims = make(map[string]uint32)
		}
		attacker.life.victims[victim.member.Account]++
		gain := s.authority.Scoring().Kill(attacker.life.streak, attacker.life.victims[victim.member.Account])
		boosted := s.consumableXP(attacker, gain)
		attacker.life.xpBonus = uint32(min(uint64(attacker.life.xpBonus)+uint64(boosted-gain), 2147483647))
		attacker.life.score = uint32(min(uint64(attacker.life.score)+uint64(gain), 2147483647))
		if s.loading.Mode == TeamDeathmatch || s.loading.Mode == TeamInstagib {
			team := attacker.player.Beacon[0]
			if team >= 1 && team <= 2 {
				s.teamScores[team-1]++
			}
		}
	}
	for _, w := range victim.weapons {
		w.ammo.CancelReload(now)
	}
	deathRecord := game.Death{Attacker: attacker.player.Slot, Victim: victim.player.Slot, Weapon: weapon, Part: partID, Source: source}
	if partID == 3 {
		deathRecord.Flags |= 1
	}
	death, _ := deathRecord.Message()
	for _, p := range s.peers {
		if p.sceneReady && !p.departed {
			if err := s.emit(p, death, true, now, out); err != nil {
				return err
			}
		}
	}
	if s.killLimitReached(attacker) {
		ending, err := s.finishOrOvertime(now)
		*out = append(*out, ending...)
		return err
	}
	return nil
}
