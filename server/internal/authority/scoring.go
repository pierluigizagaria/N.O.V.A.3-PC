package authority

// Base scoreboard points before XP-only perks/consumable factors. The native
// AddIngamePoints stores the unscaled second argument as the match score.
type Scoring struct {
	FirstKill           int32 `json:"first_kill"`
	StreakPerKill       int32 `json:"streak_per_kill"`
	ThirdKillSameVictim int32 `json:"third_kill_same_victim"`
	WinBonus            int32 `json:"win_bonus"`
	CoinsPercent        int32 `json:"coins_percent"`
}

func (s Scoring) valid() bool {
	return s.FirstKill > 0 && s.FirstKill <= 1000000 && s.StreakPerKill > 0 && s.StreakPerKill <= 1000000 && s.ThirdKillSameVictim >= 0 && s.ThirdKillSameVictim <= 1000000 && s.WinBonus >= 0 && s.WinBonus <= 1000000 && s.CoinsPercent >= 0 && s.CoinsPercent <= 100
}

func (s Scoring) Kill(streak, sameVictim uint32) uint32 {
	points := uint64(s.StreakPerKill) * uint64(streak)
	if streak == 1 {
		points = uint64(s.FirstKill)
	}
	if sameVictim == 3 {
		points += uint64(s.ThirdKillSameVictim)
	}
	return uint32(min(points, 2147483647))
}
