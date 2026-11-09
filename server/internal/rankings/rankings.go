// Package rankings stores server-produced results and serves stable ranking views.
// No client upload is accepted as authoritative match or XP evidence.
package rankings

import (
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"errors"
	"sort"

	"nova3.test/backend/internal/persistence"
)

const Maximum = 2147483647

type Stats struct {
	Matches, Kills, Deaths, XP, Wins, Headshots, Flags, Seconds, Shots, Hits, Resets int
}

type Entry struct {
	Account, Name string
	Position      int
	Stats
}

type Service struct {
	Store                    *persistence.Store
	InitialXP, InitialResets int
}

func (s Service) seed(tx *sql.Tx) error {
	if s.InitialXP < 0 || s.InitialXP > Maximum || s.InitialResets < 0 || s.InitialResets > Maximum {
		return errors.New("invalid initial ranking policy")
	}
	_, err := tx.Exec(`INSERT INTO player_stats(account,xp,resets) SELECT account,?,? FROM profiles WHERE true ON CONFLICT(account) DO NOTHING`, s.InitialXP, s.InitialResets)
	return err
}

const ordered = `WITH ranked AS (SELECT a.id,a.name,row_number() OVER(ORDER BY p.xp DESC,p.kills DESC,a.name,a.id) AS position,
p.matches,p.kills,p.deaths,p.xp,p.wins,p.headshots,p.flags,p.seconds,p.shots,p.hits,p.resets
FROM player_stats p JOIN accounts a ON a.id=p.account) SELECT * FROM ranked `

type scanner interface{ Scan(...any) error }

func read(row scanner) (e Entry, err error) {
	err = row.Scan(&e.Account, &e.Name, &e.Position, &e.Matches, &e.Kills, &e.Deaths, &e.XP, &e.Wins, &e.Headshots, &e.Flags, &e.Seconds, &e.Shots, &e.Hits, &e.Resets)
	return
}

// View reads the caller and a bounded page in one transaction. For an around
// query, radius is the number of neighboring ranks on each side.
func (s Service) View(account string, count, radius int) (self Entry, entries []Entry, err error) {
	if count < 0 || count > 100 || radius < 0 || radius > 49 {
		return self, nil, errors.New("invalid ranking page")
	}
	err = s.Store.Write(func(tx *sql.Tx) error {
		if err := s.seed(tx); err != nil {
			return err
		}
		var err error
		self, err = read(tx.QueryRow(ordered+"WHERE id=?", account))
		if err != nil {
			return err
		}
		start := 0
		if radius > 0 {
			start = max(0, self.Position-1-radius)
			count = radius*2 + 1
		}
		rows, err := tx.Query(ordered+"ORDER BY position LIMIT ? OFFSET ?", count, start)
		if err != nil {
			return err
		}
		defer rows.Close()
		for rows.Next() {
			e, err := read(rows)
			if err != nil {
				return err
			}
			entries = append(entries, e)
		}
		return rows.Err()
	})
	return
}

type Result struct {
	Account                                                         string
	Kills, Deaths, XP, Wins, Headshots, Flags, Seconds, Shots, Hits int
	Coins                                                           int `json:",omitempty"`
}

// Record is called only by match authority after finalization. The entire
// roster commits once; retries cannot duplicate XP or partially apply results.
func (s Service) Record(matchID string, results []Result) error {
	return s.record(matchID, results, true)
}

// Checkpoint commits cumulative authoritative counters while a room remains
// open. The per-account ledger applies only the delta, including after rejoin
// or retry; match participation is counted once, not once per checkpoint.
func (s Service) Checkpoint(matchID string, results []Result) error {
	return s.record(matchID, results, false)
}

func (s Service) record(matchID string, results []Result, final bool) error {
	// A twelve-slot match may have more participants after departures/rejoins.
	if matchID == "" || len(matchID) > 128 || len(results) < 1 {
		return errors.New("invalid match result")
	}
	results = append([]Result(nil), results...)
	sort.Slice(results, func(i, j int) bool { return results[i].Account < results[j].Account })
	for i, r := range results {
		if r.Account == "" || (i > 0 && results[i-1].Account == r.Account) || r.Wins > 1 || r.Headshots > r.Kills || r.Hits > r.Shots {
			return errors.New("invalid participant result")
		}
		for _, v := range []int{r.Kills, r.Deaths, r.XP, r.Wins, r.Headshots, r.Flags, r.Seconds, r.Shots, r.Hits, r.Coins} {
			if v < 0 || v > Maximum {
				return errors.New("invalid result counter")
			}
		}
	}
	body, _ := json.Marshal(results)
	hash := sha256.Sum256(body)
	digest := hex.EncodeToString(hash[:])
	return s.Store.Write(func(tx *sql.Tx) error {
		var previous string
		err := tx.QueryRow("SELECT digest FROM scored_matches WHERE id=?", matchID).Scan(&previous)
		if err == nil {
			if previous != digest {
				return errors.New("conflicting finalized match")
			}
			return nil
		}
		if !errors.Is(err, sql.ErrNoRows) {
			return err
		}
		if err = s.seed(tx); err != nil {
			return err
		}
		for _, r := range results {
			prior := Result{}
			var raw string
			matches := 0
			err := tx.QueryRow("SELECT body FROM match_progress WHERE match=? AND account=?", matchID, r.Account).Scan(&raw)
			if errors.Is(err, sql.ErrNoRows) {
				matches = 1
			} else if err != nil {
				return err
			} else if err = json.Unmarshal([]byte(raw), &prior); err != nil {
				return err
			}
			delta := []int{r.Kills - prior.Kills, r.Deaths - prior.Deaths, r.XP - prior.XP, r.Wins - prior.Wins, r.Headshots - prior.Headshots, r.Flags - prior.Flags, r.Seconds - prior.Seconds, r.Shots - prior.Shots, r.Hits - prior.Hits, r.Coins - prior.Coins}
			for _, v := range delta {
				if v < 0 {
					return errors.New("authoritative result counter regressed")
				}
			}
			updated, err := tx.Exec(`UPDATE player_stats SET matches=min(2147483647,matches+?),kills=min(2147483647,kills+?),deaths=min(2147483647,deaths+?),xp=min(2147483647,xp+?),wins=min(2147483647,wins+?),headshots=min(2147483647,headshots+?),flags=min(2147483647,flags+?),seconds=min(2147483647,seconds+?),shots=min(2147483647,shots+?),hits=min(2147483647,hits+?) WHERE account=?`, matches, delta[0], delta[1], delta[2], delta[3], delta[4], delta[5], delta[6], delta[7], delta[8], r.Account)
			if err != nil {
				return err
			}
			if n, _ := updated.RowsAffected(); n != 1 {
				return errors.New("unknown match participant")
			}
			if _, err = tx.Exec(`INSERT INTO wallets(account,coins) VALUES(?,?) ON CONFLICT(account) DO UPDATE SET coins=min(2147483647,wallets.coins+excluded.coins)`, r.Account, delta[9]); err != nil {
				return err
			}
			body, _ := json.Marshal(r)
			if _, err = tx.Exec(`INSERT INTO match_progress(match,account,body) VALUES(?,?,?) ON CONFLICT(match,account) DO UPDATE SET body=excluded.body`, matchID, r.Account, string(body)); err != nil {
				return err
			}
		}
		if !final {
			return nil
		}
		var count int
		if err = tx.QueryRow("SELECT count(*) FROM match_progress WHERE match=?", matchID).Scan(&count); err != nil {
			return err
		}
		if count != len(results) {
			return errors.New("final result omitted a participant")
		}
		_, err = tx.Exec("INSERT INTO scored_matches(id,digest) VALUES(?,?)", matchID, digest)
		return err
	})
}
