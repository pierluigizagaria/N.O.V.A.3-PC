package match

import "time"

// Native mode identities are protocol constants, not the map availability bits.
const (
	FreeForAll byte = iota
	TeamDeathmatch
	FreezeTag
	CaptureTheFlag
	CaptureThePoint
	Instagib
	TeamInstagib
)

func SupportedMode(mode byte) bool {
	return mode <= TeamInstagib
}

func teamMode(mode byte) bool {
	return mode == TeamDeathmatch || mode == FreezeTag || mode == CaptureTheFlag || mode == CaptureThePoint || mode == TeamInstagib
}

func modeMapBit(mode byte) int32 {
	bits := [...]int32{2, 4, 8, 32, 64, 16, 128}
	if int(mode) >= len(bits) {
		return 0
	}
	return bits[mode]
}

func (s *Session) assignTeam(p *peer) {
	if !teamMode(s.loading.Mode) {
		p.player.Beacon[0] = p.player.Slot + 1
		return
	}
	var count [2]int
	for _, other := range s.peers {
		if other != p && other.joined && !other.departed && other.player.Beacon[0] >= 1 && other.player.Beacon[0] <= 2 {
			count[other.player.Beacon[0]-1]++
		}
	}
	p.player.Beacon[0] = 1
	if count[1] < count[0] {
		p.player.Beacon[0] = 2
	}
}

func (s *Session) teammates(a, b *peer) bool {
	return a != b && teamMode(s.loading.Mode) && a.player.Beacon[0] == b.player.Beacon[0]
}

func (s *Session) canDamage(a, b *peer) bool {
	// StartLoading flag2 is native GameMpManager+3bfa, friendly fire.
	return !s.teammates(a, b) || s.loading.Flags[2] != 0
}

func (s *Session) killLimitReached(p *peer) bool {
	if s.loading.Parameter2 == 0 {
		return false
	}
	if s.loading.Mode == TeamDeathmatch || s.loading.Mode == TeamInstagib {
		return s.teamScores[0] >= int32(s.loading.Parameter2) || s.teamScores[1] >= int32(s.loading.Parameter2)
	}
	return !teamMode(s.loading.Mode) && p.life.kills >= s.loading.Parameter2
}

// The original client adds three minutes at a tied result boundary. Keep the
// server clock in step with its one-byte overtime notification.
func (s *Session) finishOrOvertime(now time.Time) ([]Delivery, error) {
	tied := false
	if teamMode(s.loading.Mode) {
		tied = s.teamScores[0] == s.teamScores[1]
	} else {
		var best uint32
		leaders := 0
		for _, p := range s.peers {
			if p.departed || !p.life.participated {
				continue
			}
			if p.life.score > best {
				best, leaders = p.life.score, 1
			} else if p.life.score == best {
				leaders++
			}
		}
		tied = leaders > 1
	}
	if !tied {
		return s.endGame(now)
	}
	data, err := loadModeData()
	if err != nil {
		return nil, err
	}
	minutes := data.Rules.OvertimeMinutes
	s.loading.Parameter1 += uint32(minutes)
	s.endsAt = now.Add(time.Duration(minutes) * time.Minute)
	var out []Delivery
	for _, p := range s.peers {
		if p.sceneReady && !p.departed {
			if err := s.emit(p, []byte{59}, true, now, &out); err != nil {
				return nil, err
			}
		}
	}
	return out, nil
}
