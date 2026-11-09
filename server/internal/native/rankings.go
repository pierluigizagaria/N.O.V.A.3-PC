package native

import (
	"errors"
	"strconv"
	"strings"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/protocol/legacy"
	"nova3.test/backend/internal/rankings"
)

func numbers(values ...int) string {
	parts := make([]string, len(values))
	for i, v := range values {
		parts[i] = strconv.Itoa(v)
	}
	return strings.Join(parts, "|") + "|"
}

func rankingRow(e rankings.Entry) string {
	name := e.Name
	if len(name) > 16 {
		name = name[:16]
	}
	return numbers(e.Position) + name + "|" + numbers(e.Matches, e.Kills, e.Deaths, e.XP, e.XP, e.Resets)
}

func rankingSelf(e rankings.Entry) string {
	// Original processRanking: the self record occupies fields 0..20;
	// the eight-field ranking rows follow. Unused prizes/MHP remain absent.
	return "y|" + numbers(e.Position, 0, e.Matches, e.Kills, e.Deaths, e.XP, e.Wins, e.Headshots, e.Flags, e.Seconds, e.Shots, e.Hits, 0, e.Resets, 0) + "|0||0|0|"
}

func (s *Services) ranking(r legacy.Request) (string, error) {
	if _, err := nativeInteger(r.Fields, "tid", 0, 0, 0); err != nil {
		return "", errors.New("unknown tournament")
	}
	service := rankings.Service{Store: s.Store, InitialXP: s.Shop.Mapping.Policy.XP, InitialResets: s.Shop.Mapping.Policy.Rank}
	account := r.Fields["u"]
	if r.Function == 614 {
		// Despite the key un, this game's SendGetRankingPlayer passes the
		// selected player's name (the same value used to obtain its avatar).
		id, err := accounts.FindPublic(s.Store, r.Fields["un"], "")
		if err != nil {
			return "n", nil
		}
		e, _, err := service.View(id, 0, 0)
		if err != nil {
			return "", err
		}
		// processRankingPlayer is a player summary, not a leaderboard row.
		// getMHPInfo uses field5 as reincarnation rank for GetLevelFromXp;
		// its last two fields are the shot/hit denominator and numerator.
		return numbers(e.Matches, e.Wins, e.Kills, e.Deaths, e.XP, e.Resets, e.Headshots, e.Shots, e.Hits), nil
	}
	count, err := nativeInteger(r.Fields, "p", 10, 1, 100)
	if err != nil {
		return "", err
	}
	radius := 0
	if r.Function == 613 {
		radius = min(count, 49)
	}
	self, entries, err := service.View(account, count, radius)
	if err != nil {
		return "", err
	}
	var body strings.Builder
	if r.Function == 612 {
		_, err := nativeInteger(r.Fields, "x", 0, 0, 1)
		if err != nil {
			return "", err
		}
		// The original login sends SendGetRankingTop(0,1,0), then reads the
		// personal fields populated by processRanking's y prefix. x=0 is not
		// permission to omit those fields; n would leave them at their defaults.
		body.WriteString(rankingSelf(self))
	}
	for _, e := range entries {
		body.WriteString(rankingRow(e))
	}
	return body.String(), nil
}
