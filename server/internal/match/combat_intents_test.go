package match

import (
	"errors"
	"nova3.test/backend/internal/protocol/game"
	"testing"
)

func TestUnimplementedGrenadeAndMeleeDrainWithoutAuthoritativeEffects(t *testing.T) {
	s, now := loadoutFixture(t)
	p := s.peers[0]
	before := p.player
	grenade, _ := (game.GrenadeThrow{Actor: 0, Speed: 1.5}).Message()
	melee, _ := (game.MeleeRequest{Victim: 1}).Message()
	for sequence, body := range [][]byte{grenade, melee} {
		out, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: uint32(sequence)}, body), now)
		if err != nil || len(out) != 1 || out[0].To != p.member.Endpoint || out[0].Data[game.HeaderSize] != game.Acknowledge {
			t.Fatal("combat intent did not drain", out, err)
		}
		if p.player != before || p.weapon != 11 {
			t.Fatal("unimplemented combat changed authority")
		}
	}
	foreign, _ := (game.GrenadeThrow{Actor: 1}).Message()
	missing, _ := (game.MeleeRequest{Victim: 9}).Message()
	self, _ := (game.MeleeRequest{Victim: 0}).Message()
	for _, body := range [][]byte{foreign, missing, self} {
		if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: 2}, body), now); !errors.Is(err, ErrPeer) {
			t.Fatal("invalid actor/target admitted", err)
		}
	}
}

func TestPurchasedClaimDrainsWithoutGrantingEquipment(t *testing.T) {
	s, now := loadoutFixture(t)
	p := s.peers[0]
	before := p.player
	for sequence, flags := range []byte{0, 0xa5, 0xff} {
		body := []byte{game.ClientPurchasedInfo, p.player.Slot, flags}
		out, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: uint32(sequence)}, body), now)
		if err != nil || len(out) != 1 || out[0].To != p.member.Endpoint || out[0].Data[game.HeaderSize] != game.Acknowledge {
			t.Fatal("purchase claim blocks the reliable queue", out, err)
		}
		if p.player != before || p.weapon != 11 {
			t.Fatal("client purchase claim changed equipment authority")
		}
	}
	if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: 3}, []byte{game.ClientPurchasedInfo, 1, 0}), now); !errors.Is(err, ErrPeer) {
		t.Fatal("foreign purchase claim admitted", err)
	}
}

func TestPurchasedClaimCannotBlockRoundSpawnBundle(t *testing.T) {
	s, now := loadoutFixture(t)
	p := s.peers[0]
	s.round.runningSince = now
	p.roundSpawnPending = true
	before := s.movement.poses[0].Generation
	loadout, _ := p.player.Loadout()
	loadout.Selections[2] = 2
	equipment, _ := loadout.Message()
	out, err := s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true},
		[]byte{game.ClientPurchasedInfo, 0, 0xff}, equipment, []byte{game.RequestRespawn, 0}), now)
	if err != nil || p.roundSpawnPending || s.movement.poses[0].Generation != before+1 || p.weapon != 12 {
		t.Fatal("post-load reliable bundle did not spawn with the owned loadout", err)
	}
	respawn := false
	for _, d := range out {
		packet, err := game.Decode(d.Data)
		if err != nil {
			t.Fatal(err)
		}
		for _, record := range packet.Messages {
			respawn = respawn || record[0] == game.RespawnPlayer
		}
	}
	if !respawn {
		t.Fatal("no native respawn delivered")
	}
}
