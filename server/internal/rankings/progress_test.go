package rankings

import (
	"path/filepath"
	"testing"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/testfixture"
)

func TestProgressRejoinRetryAndFinalizationApplyOnlyDeltas(t *testing.T) {
	db, err := persistence.Open(filepath.Join(t.TempDir(), "progress.db"))
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	c, _ := testfixture.NativeCatalogue()
	if err = db.Admit(c); err != nil {
		t.Fatal(err)
	}
	a, err := accounts.Create(db, "alice")
	if err != nil {
		t.Fatal(err)
	}
	if err = grants.Provision(db, a.ID, c.Version); err != nil {
		t.Fatal(err)
	}
	s := Service{Store: db, InitialXP: 100}
	r := []Result{{Account: a.ID, Kills: 2, XP: 30, Coins: 6, Shots: 5, Hits: 2}}
	for range 2 {
		if err = s.Checkpoint("room", r); err != nil {
			t.Fatal(err)
		}
	}
	// The account left and later rejoined: the same cumulative match ledger
	// gains one kill, not a second match or another copy of its previous coins.
	r[0].Kills, r[0].XP, r[0].Coins, r[0].Shots, r[0].Hits = 3, 60, 13, 9, 3
	if err = s.Checkpoint("room", r); err != nil {
		t.Fatal(err)
	}
	bad := append([]Result(nil), r...)
	bad[0].Coins--
	if err = s.Checkpoint("room", bad); err == nil {
		t.Fatal("regressed reward accepted")
	}
	bad = append([]Result(nil), r...)
	bad[0].Kills++
	bad = append(bad, Result{Account: "unknown", Kills: 1})
	if err = s.Checkpoint("room", bad); err == nil {
		t.Fatal("unknown participant accepted")
	}
	r[0].Wins, r[0].XP, r[0].Coins = 1, 110, 25
	for range 2 {
		if err = s.Record("room", r); err != nil {
			t.Fatal(err)
		}
	}
	self, _, err := s.View(a.ID, 0, 0)
	if err != nil || self.Matches != 1 || self.Kills != 3 || self.XP != 210 || self.Wins != 1 || self.Shots != 9 || self.Hits != 3 {
		t.Fatal(self, err)
	}
	var coins int
	if err = db.DB.QueryRow("SELECT coins FROM wallets WHERE account=?", a.ID).Scan(&coins); err != nil || coins != 25 {
		t.Fatal(coins, err)
	}
	r[0].XP++
	if err = s.Checkpoint("room", r); err == nil {
		t.Fatal("completed match changed")
	}
}
