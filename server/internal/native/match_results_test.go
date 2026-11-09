package native

import (
	"errors"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/match"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/protocol/legacy"
)

func TestFinalResultsRetryThenSurviveLoginAndRestart(t *testing.T) {
	s, account, path, _ := fixture(t)
	h := &matchHub{recordResults: s.recordMatchResults}
	a := &allocation{resultID: "completed-room", resultsCaptured: true, results: []match.Result{{Account: account, Kills: 4, Deaths: 2, XP: 90, Wins: 1, Headshots: 1, Shots: 12, Hits: 7, Coins: 20}}}
	// A failed write keeps the same result; successful retries commit only once.
	h.recordResults = func(string, []match.Result, bool) error { return errors.New("storage unavailable") }
	if err := h.saveResults(a, time.Now()); err == nil || a.resultsSaved {
		t.Fatal("lost failed result")
	}
	h.recordResults = s.recordMatchResults
	for range 2 {
		if err := h.saveResults(a, time.Now()); err != nil {
			t.Fatal(err)
		}
	}
	if err := s.recordMatchResults(a.resultID, a.results, true); err != nil {
		t.Fatal(err)
	}
	if err := s.Store.Close(); err != nil {
		t.Fatal(err)
	}
	reopened, err := persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer reopened.Close()
	s.Store = reopened
	server := httptest.NewTLSServer(s.Handler())
	defer server.Close()
	client := server.Client()
	uid := loginUID(t, request(t, client, "GET", server.URL+"/legacy?b="+legacy.Encode([]byte("f|15|u|alice|p|native-test-password|")), "", 200))
	stats := request(t, client, "GET", server.URL+"/legacy?b="+legacy.Encode([]byte("g|112|u|"+uid+"|tid|0|p|1|x|1|")), "", 200)
	if !strings.Contains(stats, "y|1|0|1|4|2|190|1|1|0|0|12|7|") {
		t.Fatal("saved statistics missing after login", stats)
	}
	token, err := accounts.Issue(reopened, account, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	storage := Storage{DB: reopened, Shop: s.Shop}
	body, err := storage.Access(token, "me", "key2_01", nil)
	if err != nil || !strings.HasPrefix(body, "20|") {
		t.Fatal("wallet lost", body, err)
	}
	for _, claimed := range []string{"0", "2147483647"} {
		fields := strings.Split(body, "|")
		fields[0] = claimed
		update := strings.Join(fields, "|")
		got, err := storage.Access(token, "me", "key2_01", &update)
		if err != nil || got != body {
			t.Fatal("client overwrote authoritative wallet", got, err)
		}
	}
}

func TestProgressIsReturnedAtLoginBeforeRoomFinishes(t *testing.T) {
	s, account, path, _ := fixture(t)
	rows := []match.Result{{Account: account, Kills: 2, Deaths: 1, XP: 30, Coins: 6, Shots: 5, Hits: 3}}
	if err := s.recordMatchResults("still-playing", rows, false); err != nil {
		t.Fatal(err)
	}
	var finalized int
	if err := s.Store.DB.QueryRow("SELECT count(*) FROM scored_matches").Scan(&finalized); err != nil || finalized != 0 {
		t.Fatal("test accidentally finalized the room", finalized, err)
	}
	if err := s.Store.Close(); err != nil {
		t.Fatal(err)
	}
	db, err := persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	s.Store = db
	if _, err = s.dispatchLegacy(legacy.Request{Function: 15, Fields: map[string]string{"u": "alice", "p": "native-test-password"}}); err != nil {
		t.Fatal(err)
	}
	// Match the original login's SendGetRankingTop(0,1,0), not an invented x=1.
	stats, err := s.dispatchLegacy(legacy.Request{Function: 612, Fields: map[string]string{"u": account, "p": "1", "x": "0"}})
	if err != nil || !strings.HasPrefix(stats, "y|1|0|1|2|1|130|") {
		t.Fatal("progress hidden until room end", stats, err)
	}
	token, err := accounts.Issue(db, account, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	wallet, err := (Storage{DB: db, Shop: s.Shop}).Access(token, "me", "key2_01", nil)
	if err != nil || !strings.HasPrefix(wallet, "6|") {
		t.Fatal("coins hidden until room end", wallet, err)
	}
}
