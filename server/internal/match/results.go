package match

import (
	"sort"
	"time"
)

// Result contains only counters derived by match authority. XP currently uses
// the supported base scoring path; XP perks and achievement rewards are pending.
type Result struct {
	Account                                                                string
	Kills, Deaths, XP, Wins, Headshots, Seconds, Shots, Hits, Coins, Flags int
}

func (b *Bound) Results(now time.Time) []Result {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.session.Results(now)
}

func (s *Session) Results(now time.Time) []Result {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.authority == nil {
		return nil
	}
	completed := !s.resultTime.IsZero()
	if completed {
		now = s.resultTime
	}
	elapsed := s.roundClock(now).Elapsed
	best := uint32(0)
	for _, p := range s.peers {
		if p.life.participated && !p.departed {
			best = max(best, p.life.score)
		}
	}
	byAccount := make(map[string]Result)
	participants := append(append([]*peer(nil), s.retiredPeers...), s.peers...)
	for _, p := range participants {
		l := p.life
		if !l.participated {
			continue
		}
		r := byAccount[p.member.Account]
		r.Account = p.member.Account
		r.Kills += int(l.kills)
		r.Deaths += int(l.deaths)
		r.XP += int(min(uint64(l.score)+uint64(l.xpBonus), 2147483647))
		r.Headshots += int(l.headshots)
		r.Flags += int(l.flags)
		r.Shots += int(l.shots)
		r.Hits += int(l.hits)
		end := elapsed
		if l.playStopped {
			end = min(end, l.playedTo)
		}
		if end > l.playedFrom {
			r.Seconds += int((end - l.playedFrom) / 1000)
		}
		// Native FFA grants a win when no remaining opponent has a higher
		// score. A quit/aborted match never grants a win bonus.
		won := l.score == best
		if teamMode(s.loading.Mode) {
			team := p.player.Beacon[0]
			won = team >= 1 && team <= 2 && s.teamScores[team-1] > s.teamScores[2-team]
		}
		if completed && !p.departed && won {
			r.Wins = 1
			r.XP += int(s.consumableXP(p, uint32(s.authority.Scoring().WinBonus)))
		}
		byAccount[r.Account] = r
	}
	var out []Result
	for _, r := range byAccount {
		// Base combat rewards survive leaving the room. Compute the cumulative
		// conversion once per account/match so checkpoints do not lose rounding.
		r.Coins = int(min(int64(r.XP)*int64(s.authority.Scoring().CoinsPercent)/100, 2147483647))
		out = append(out, r)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Account < out[j].Account })
	return out
}
