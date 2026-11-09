package rankings

import (
	"path/filepath"
	"testing"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/testfixture"
)

func TestFinalizedScoresAreAtomicPersistentAndIdempotent(t *testing.T) {
	path := filepath.Join(t.TempDir(), "rankings.db")
	db, err := persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	c, _ := testfixture.NativeCatalogue()
	if err = db.Admit(c); err != nil {
		t.Fatal(err)
	}
	var ids []string
	for _, name := range []string{"alice", "bob", "charlie"} {
		a, err := accounts.Create(db, name)
		if err != nil {
			t.Fatal(err)
		}
		if err = grants.Provision(db, a.ID, c.Version); err != nil {
			t.Fatal(err)
		}
		ids = append(ids, a.ID)
	}
	s := Service{Store: db, InitialXP: 100, InitialResets: 2}
	self, top, err := s.View(ids[1], 2, 0)
	if err != nil || self.Position != 2 || len(top) != 2 || top[0].Name != "alice" {
		t.Fatal("stable ties", self, top, err)
	}
	results := []Result{{Account: ids[0], Kills: 2, Deaths: 3, XP: 20, Seconds: 60}, {Account: ids[1], Kills: 3, Deaths: 2, XP: 40, Wins: 1, Headshots: 2, Shots: 10, Hits: 7, Seconds: 60}}
	if err = s.Record("match-one", results); err != nil {
		t.Fatal(err)
	}
	if err = s.Record("match-one", []Result{results[1], results[0]}); err != nil {
		t.Fatal("idempotent retry", err)
	}
	self, top, err = s.View(ids[1], 1, 1)
	if err != nil || self.Position != 1 || self.Matches != 1 || self.XP != 140 || len(top) != 3 || self.Resets != 2 {
		t.Fatal("recorded stats", self, err)
	}
	results[0].XP++
	if err = s.Record("match-one", results); err == nil {
		t.Fatal("conflicting replay accepted")
	}
	if err = s.Record("bad-roster", []Result{results[0], {Account: "missing", XP: 50}}); err == nil {
		t.Fatal("partial roster accepted")
	}
	if err = s.Record("duplicate", []Result{results[0], results[0]}); err == nil {
		t.Fatal("duplicate participant accepted")
	}
	self, _, err = s.View(ids[0], 0, 0)
	if err != nil || self.XP != 120 || self.Matches != 1 {
		t.Fatal("partial write escaped rollback", self, err)
	}
	if err = db.Close(); err != nil {
		t.Fatal(err)
	}
	db, err = persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	s.Store = db
	self, _, err = s.View(ids[1], 0, 0)
	if err != nil || self.Kills != 3 || self.XP != 140 || self.Position != 1 {
		t.Fatal("ranking lost after restart", self, err)
	}
	if err = s.Record("overflow", []Result{{Account: ids[1], XP: Maximum}}); err != nil {
		t.Fatal(err)
	}
	self, _, _ = s.View(ids[1], 0, 0)
	if self.XP != Maximum {
		t.Fatal("32-bit native counter overflow")
	}
}
