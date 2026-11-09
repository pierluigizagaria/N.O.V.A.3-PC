package match

import (
	"bytes"
	"errors"
	"testing"
	"time"

	"nova3.test/backend/internal/protocol/game"
)

func TestPresetChatAuthenticatesSenderAndDrainsDuplicates(t *testing.T) {
	s := movementFixture(t, 0).session
	now := time.Unix(100, 0)
	s.phase, s.deadline = Playing, now.Add(time.Minute)
	for _, p := range s.peers {
		p.joined, p.loaded, p.sceneReady = true, true, true
	}
	p := s.peers[0]
	body, _ := (game.Chat{Actor: 0, MessageID: 1380}).Message()
	request := wire(t, game.Header{Reliable: true}, body)
	out, err := s.Handle(p.member.Endpoint, request, now)
	if err != nil || len(out) != 2 || out[0].To != s.peers[1].member.Endpoint || !bytes.Equal(out[0].Data[13:], body) || out[0].Data[0] != 1 {
		t.Fatal(out, err)
	}
	// Retransmission ACKs without displaying a second copy.
	out, err = s.Handle(p.member.Endpoint, request, now.Add(time.Millisecond))
	if err != nil || len(out) != 1 || out[0].Data[13] != game.Acknowledge {
		t.Fatal(out, err)
	}
	// Two fresh records in a single packet also obey the per-actor cooldown.
	out, err = s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: 1}, body, body), now.Add(time.Second))
	if err != nil || len(out) != 1 {
		t.Fatal(out, err)
	}
	out, err = s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: 2}, body), now.Add(2*time.Second))
	if err != nil || len(out) != 2 {
		t.Fatal(out, err)
	}
	for _, v := range []game.Chat{{Actor: 1, MessageID: 1380}, {Actor: 0, MessageID: 0xffffffff}, {Team: 1, Actor: 0, MessageID: 1380}} {
		b, _ := v.Message()
		if _, err = s.Handle(p.member.Endpoint, wire(t, game.Header{Reliable: true, Sequence: 3}, b), now.Add(4*time.Second)); !errors.Is(err, ErrPeer) {
			t.Fatal("invalid chat sender/content admitted", v, err)
		}
	}
	if _, err = s.Handle(p.member.Endpoint, wire(t, game.Header{}, body), now); !errors.Is(err, ErrState) {
		t.Fatal("unreliable chat admitted", err)
	}
}
