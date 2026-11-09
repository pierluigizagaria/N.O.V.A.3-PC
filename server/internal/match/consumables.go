package match

import (
	"crypto/rand"
	"math"
	"math/big"
	"time"

	"nova3.test/backend/internal/protocol/game"
)

type consumableState struct {
	ready        [9]time.Time
	until        [9]time.Time
	used         [9]uint32
	xpMultiplier float32
}

func (s *Session) activateConsumable(p *peer, m []byte, now time.Time, out *[]Delivery) error {
	v, _ := game.ReadConsumable(m)
	if s.authority == nil || s.consumeConsumable == nil || p.life.dead || p.life.frozen || p.roundSpawnPending || p.departed || !p.sceneReady || s.round.runningSince.IsZero() || now.Before(p.consumables.ready[v.Type]) {
		return nil
	}
	spent, err := s.consumeConsumable(p.member.Account, v.Type)
	if err != nil {
		return err
	}
	if !spent {
		return nil
	}
	rules := s.authority.Consumables()
	p.consumables.ready[v.Type] = now.Add(time.Duration(rules.CooldownMS) * time.Millisecond)
	p.consumables.used[v.Type]++
	var duration int32
	switch v.Type {
	case 0:
		p.life.health = s.authority.Player().BaseHealth
	case 1:
		p.consumables.xpMultiplier = rules.XPMultiplier
	case 2:
		for _, w := range p.weapons {
			w.ammo.RefillReserve(w.definition.ReserveAmmo)
		}
	case 3:
		duration = rules.PrecisionMS
	case 4:
		duration = rules.AgilityMS
	case 5:
		duration = rules.PerceptionMS
	case 6:
		duration = rules.CamouflageMS
	case 7:
		duration = rules.CountertechMS
	case 8:
		duration = rules.OverchargeMS
		p.life.health = max(1, p.life.health-rules.OverchargeHealthCost)
	}
	if duration > 0 {
		p.consumables.until[v.Type] = now.Add(time.Duration(duration) * time.Millisecond)
	}
	// Server approval is also the native owner inventory/cooldown confirmation.
	// Never relay a client's claimed activation as authority.
	message, _ := v.Message(game.ActivateConsumable)
	for _, peer := range s.peers {
		if peer.sceneReady && !peer.departed {
			if err := s.emit(peer, message, true, now, out); err != nil {
				return err
			}
		}
	}
	return nil
}

func (s *Session) consumableXP(p *peer, base uint32) uint32 {
	if p.consumables.xpMultiplier <= 1 {
		return base
	}
	return uint32(min(math.Trunc(float64(float32(base)*p.consumables.xpMultiplier)), 2147483647))
}

// Precision belongs to MP_BulletHit, not splash, melee or vehicle contacts.
func (s *Session) consumableImpact(attacker, victim *peer, amount float32, now time.Time) (float32, error) {
	if attacker.mount != nil || !now.Before(attacker.consumables.until[3]) || now.Before(victim.consumables.until[7]) || now.Before(victim.consumables.until[8]) {
		return amount, nil
	}
	n, err := rand.Int(rand.Reader, big.NewInt(100))
	if err != nil {
		return 0, err
	}
	rules := s.authority.Consumables()
	if float32(n.Int64())*.01 < rules.CriticalChance {
		amount *= rules.CriticalMultiplier
	}
	return amount, nil
}
