package native

import (
	"nova3.test/backend/internal/match"
	"nova3.test/backend/internal/rankings"
	"reflect"
	"time"
)

func (s *Services) recordMatchResults(id string, results []match.Result, final bool) error {
	rows := make([]rankings.Result, len(results))
	for i, r := range results {
		rows[i] = rankings.Result{Account: r.Account, Kills: r.Kills, Deaths: r.Deaths,
			XP: r.XP, Wins: r.Wins, Headshots: r.Headshots, Seconds: r.Seconds,
			Shots: r.Shots, Hits: r.Hits, Coins: r.Coins, Flags: r.Flags}
	}
	service := rankings.Service{Store: s.Store, InitialXP: s.Shop.Mapping.Policy.XP,
		InitialResets: s.Shop.Mapping.Policy.Rank}
	if final {
		return service.Record(id, rows)
	}
	return service.Checkpoint(id, rows)
}

// Persist while the room is still playing, including its one-player waiting
// phase. A departing account can log in again without waiting for everyone
// else to leave. Identical paused snapshots do not cause database writes.
func (h *matchHub) saveProgress(a *allocation, now time.Time) error {
	if a.resultsCaptured || h.recordResults == nil || now.Before(a.nextCheckpoint) {
		return nil
	}
	a.nextCheckpoint = now.Add(time.Second)
	rows := a.bound.Results(now)
	if len(rows) == 0 || reflect.DeepEqual(rows, a.progressSaved) {
		return nil
	}
	if err := h.recordResults(a.resultID, rows, false); err != nil {
		return err
	}
	a.progressSaved = rows
	return nil
}

// Called before publishing EndGame, and again on retirement. Database errors
// retain the same immutable result for retry, so rewards cannot be recomputed
// from a later clock or delivered twice. No lobby/client upload supplies rows.
func (h *matchHub) saveResults(a *allocation, now time.Time) error {
	if a.resultsSaved || h.recordResults == nil {
		return nil
	}
	if !a.resultsCaptured {
		a.results = a.bound.Results(now)
		a.resultsCaptured = true
	}
	if len(a.results) != 0 {
		if err := h.recordResults(a.resultID, a.results, true); err != nil {
			return err
		}
	}
	a.resultsSaved = true
	return nil
}
