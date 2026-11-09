package match

import (
	"net/netip"
	"reflect"
	"testing"
	"time"
)

func TestLethalDamageFinalizesStableResults(t *testing.T) {
	s, now, _ := shotFixture(t)
	for _, p := range s.peers {
		s.resetLife(p, now)
	}
	s.loading.Parameter2 = 1
	var out []Delivery
	if err := s.damageImpact(s.peers[0], s.peers[1], 100, 200, 3, now.Add(10*time.Second), &out); err != nil {
		t.Fatal(err)
	}
	if s.phase != Finishing {
		t.Fatal("kill limit did not end match")
	}
	r := s.Results(now.Add(time.Minute))
	if len(r) != 2 || r[0].Kills != 1 || r[0].Headshots != 1 || r[0].XP != 32 || r[0].Wins != 1 || r[0].Coins != 3 || r[0].Seconds != 10 || r[1].Deaths != 1 || r[1].Wins != 0 {
		t.Fatal(r)
	}
	s.Close()
	if !reflect.DeepEqual(r, s.Results(now.Add(time.Hour))) {
		t.Fatal("result changed during retirement")
	}
}

func TestResultsSurviveSlotReuseAndRejoiningAccount(t *testing.T) {
	s, now, _ := shotFixture(t)
	s.loading.Parameter2 = 0
	for _, p := range s.peers {
		s.resetLife(p, now)
	}
	var out []Delivery
	if err := s.damage(s.peers[0], s.peers[1], 100, 200, now.Add(time.Second), &out); err != nil {
		t.Fatal(err)
	}
	old := s.peers[0]
	if _, err := s.retire([]netip.AddrPort{old.member.Endpoint}, now.Add(2*time.Second)); err != nil {
		t.Fatal(err)
	}
	for _, p := range s.peers {
		p.channel.pending = nil
	}
	if _, err := s.admit(old.member, LoadoutPolicy{}, now.Add(3*time.Second)); err != nil {
		t.Fatal(err)
	}
	s.peers[0].sceneReady = true
	s.resetLife(s.peers[0], now.Add(3*time.Second))
	s.resetLife(s.peers[1], now.Add(3*time.Second))
	if err := s.damage(s.peers[0], s.peers[1], 100, 200, now.Add(4*time.Second), &out); err != nil {
		t.Fatal(err)
	}
	r := s.Results(now.Add(5 * time.Second))
	if len(r) != 2 || r[0].Kills != 2 || r[0].XP != 14 || r[0].Seconds != 4 || r[0].Coins != 1 || r[0].Wins != 0 || r[1].Deaths != 2 {
		t.Fatal(r)
	}
}
