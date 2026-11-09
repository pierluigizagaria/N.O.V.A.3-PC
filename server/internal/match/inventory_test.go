package match

import (
	"nova3.test/backend/internal/authority"
	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/testfixture"
	"testing"
	"time"
)

func inventoryFixture(t *testing.T) (*Session, time.Time) {
	t.Helper()
	s, now := loadoutFixture(t)
	var err error
	s.authority, err = authority.Decode(testfixture.NativeAuthority())
	if err != nil {
		t.Fatal(err)
	}
	s.movement.primaryWeapons = map[byte]uint16{1: 100, 2: 101}
	s.movement.secondaryWeapons = map[byte]uint16{1: 101}
	s.movement.addons = map[byte]byte{1: authority.AddonAmmo, 2: authority.AddonReload}
	for _, p := range s.peers {
		p.weapon = 100
		p.player.Beacon[7], p.player.Beacon[8] = 1, 2
		policy := s.movement.loadouts[p.member.Account]
		policy[4][2] = true
		s.movement.loadouts[p.member.Account] = policy
		if err := s.resetInventory(p); err != nil {
			t.Fatal(err)
		}
	}
	s.round.runningSince = now
	return s, now
}

func TestInventoryReloadSwitchAndReliableReplay(t *testing.T) {
	s, now := inventoryFixture(t)
	p := s.peers[0]
	w := p.weapons[100]
	if w.ammo.Snapshot() != (authority.AmmoSnapshot{Reserve: 105, Magazine: 35}) || p.weapons[101].ammo.Snapshot().Magazine != 20 {
		t.Fatal("equipment mapping", w.ammo.Snapshot())
	}
	for range 5 {
		w.ammo.Consume(now)
	}
	message, _ := (game.PlayerState{Actor: 0, State: 3}).Message()
	packet := wire(t, game.Header{Reliable: true}, message)
	out, err := s.Handle(p.member.Endpoint, packet, now)
	if err != nil || !w.ammo.Snapshot().Reloading {
		t.Fatal(out, err)
	}
	relays := 0
	for _, d := range out {
		if d.To == s.peers[1].member.Endpoint {
			relays++
		}
	}
	if relays != 1 {
		t.Fatal("reload presentation missing", out)
	}
	if _, err = s.Handle(p.member.Endpoint, packet, now.Add(100*time.Millisecond)); err != nil {
		t.Fatal(err)
	}
	// 400/1.5 rounds up to267 ms. A replay cannot postpone the refill.
	if _, err = s.Tick(now.Add(267 * time.Millisecond)); err != nil {
		t.Fatal(err)
	}
	if w.ammo.Snapshot() != (authority.AmmoSnapshot{Reserve: 100, Magazine: 35, Reloading: true}) {
		t.Fatal(w.ammo.Snapshot())
	}
	change, _ := (game.WeaponAction{Actor: 0, Value: 101}).Message(game.ChangeWeapon)
	if _, err = s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: 1}, change), now.Add(300*time.Millisecond)); err != nil {
		t.Fatal(err)
	}
	if w.ammo.Snapshot().Reloading || p.weapon != 101 {
		t.Fatal("switch did not cancel old reload")
	}
	if p.weapons[101].ammo.Snapshot() != (authority.AmmoSnapshot{Reserve: 60, Magazine: 20}) {
		t.Fatal("other weapon inventory changed")
	}
}

func TestInventoryOnlyServerSpawnCanReplenish(t *testing.T) {
	s, now := inventoryFixture(t)
	p := s.peers[0]
	w := p.weapons[100]
	w.ammo.Consume(now)
	loadout, _ := p.player.Loadout()
	loadout.Selections[2] = 2
	message, _ := loadout.Message()
	if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true}, message, []byte{game.RequestRespawn, 0}), now); err != nil {
		t.Fatal(err)
	}
	if p.weapons[100] != w || w.ammo.Snapshot().Magazine != 34 || p.weapon != 100 {
		t.Fatal("client replenished or swapped live loadout")
	}
	p.roundSpawnPending = true
	if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: 1}, []byte{game.RequestRespawn, 0}), now); err != nil {
		t.Fatal(err)
	}
	if p.weapons[100] == w || p.weapons[100].ammo.Snapshot().Magazine != 35 {
		t.Fatal("server spawn did not reset inventory")
	}
	bad := p.player
	bad.Beacon[7] = 127
	if _, err := s.makeInventory(bad); err == nil {
		t.Fatal("unknown addon mapped to none")
	}
}
