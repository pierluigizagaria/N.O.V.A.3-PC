package native

import (
	"strings"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/protocol/legacy"
	"nova3.test/backend/internal/rankings"
)

func TestNativeRankingViewsUseServerResults(t *testing.T) {
	s, alice, _, _ := fixture(t)
	bob, err := accounts.Create(s.Store, "bob")
	if err != nil {
		t.Fatal(err)
	}
	if err = grants.Provision(s.Store, bob.ID, s.Shop.Catalogue.Version); err != nil {
		t.Fatal(err)
	}
	s.Sessions.Login(alice, time.Now(), time.Hour)
	scores := rankings.Service{Store: s.Store, InitialXP: s.Shop.Mapping.Policy.XP, InitialResets: s.Shop.Mapping.Policy.Rank}
	if err = scores.Record("finished", []rankings.Result{{Account: bob.ID, Kills: 4, Deaths: 1, XP: 40, Wins: 1}}); err != nil {
		t.Fatal(err)
	}
	call := func(fn int, fields map[string]string) (string, error) {
		fields["u"] = alice
		return s.dispatchLegacy(legacy.Request{Function: fn, Fields: fields})
	}
	top, err := call(612, map[string]string{"p": "10", "x": "0"})
	rows := "1|bob|1|4|1|140|140|0|2|alice|0|0|0|100|100|0|"
	if err != nil || top != "y|2|0|0|0|0|100|0|0|0|0|0|0|0|0|0||0||0|0|"+rows {
		t.Fatal("native ranking rows", top, err)
	}
	withSelf, err := call(612, map[string]string{"p": "1", "x": "1"})
	if err != nil || !strings.HasPrefix(withSelf, "y|2|0|0|0|0|100|") || len(strings.Split(withSelf, "|")) != 30 {
		t.Fatal("self prefix or row boundaries", withSelf, err)
	}
	around, err := call(613, map[string]string{"p": "9"})
	if err != nil || around != rows {
		t.Fatal("around response", around, err)
	}
	detail, err := call(614, map[string]string{"un": "bob"})
	if err != nil || detail != "1|1|4|1|140|0|0|0|0|" {
		t.Fatal("detail response", detail, err)
	}
	if v, err := call(614, map[string]string{"un": "unknown"}); err != nil || v != "n" {
		t.Fatal("missing player", v, err)
	}
	for _, fields := range []map[string]string{{"p": "-1"}, {"p": "101"}, {"x": "2"}, {"tid": "9"}} {
		if _, err := call(612, fields); err == nil {
			t.Fatal("invalid ranking selection accepted")
		}
	}
	if _, err := call(601, map[string]string{"xp": "9999999"}); err == nil {
		t.Fatal("client minted authoritative score")
	}
}
