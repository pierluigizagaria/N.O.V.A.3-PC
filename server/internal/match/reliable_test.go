package match

import (
	"bytes"
	"math"
	"testing"
	"time"

	"nova3.test/backend/internal/protocol/game"
)

func TestReliableLossReorderDuplicateAndAckBits(t *testing.T) {
	now := time.Unix(1, 0)
	c := channel{}
	for _, seq := range []uint32{0, 2, 1} {
		fresh, err := c.receive(game.Header{Reliable: true, Sequence: seq}, now)
		if err != nil || !fresh {
			t.Fatal(seq, err)
		}
	}
	if c.header(false).Ack != 2 || c.header(false).AckMask != 7 {
		t.Fatal("inclusive ACK mask", c.header(false))
	}
	if fresh, err := c.receive(game.Header{Reliable: true, Sequence: 1}, now); err != nil || fresh {
		t.Fatal("duplicate applied", err)
	}
	first, err := c.send([]byte{game.FinishedLoading}, true, now)
	if err != nil {
		t.Fatal(err)
	}
	second, _ := c.send([]byte{game.FinishedLoading}, true, now)
	if len(c.retries(now.Add(198*time.Millisecond))) != 0 {
		t.Fatal("early retry")
	}
	out := c.retries(now.Add(199 * time.Millisecond))
	if len(out) != 2 || !bytes.Equal(out[0], first) || !bytes.Equal(out[1], second) {
		t.Fatal("retry identity")
	}
	// ACK=1/mask=1 removes sequence1 only, leaving zero for retransmission.
	if _, err = c.receive(game.Header{Ack: 1, AckMask: 1}, now.Add(time.Second)); err != nil {
		t.Fatal(err)
	}
	if len(c.pending) != 1 || c.pending[0].sequence != 0 {
		t.Fatal("off-by-one ACK")
	}
	if _, err = c.receive(game.Header{Ack: 1, AckMask: 2}, now.Add(time.Second)); err != nil || len(c.pending) != 0 {
		t.Fatal("selective ACK", err)
	}
	if len(c.retries(now.Add(2*time.Second))) != 0 {
		t.Fatal("acked packet retransmitted")
	}
}

func TestReliableBoundsAndNonWrappingSequence(t *testing.T) {
	now := time.Unix(1, 0)
	c := channel{}
	for i := 0; i < 32; i++ {
		if _, err := c.send([]byte{game.Acknowledge}, true, now); err != nil {
			t.Fatal(err)
		}
	}
	if _, err := c.send([]byte{game.Acknowledge}, true, now); err != ErrWindow {
		t.Fatal("unbounded send queue", err)
	}
	if _, err := c.receive(game.Header{Reliable: true, Sequence: 256, Ack: 31, AckMask: 0xffffffff}, now); err == nil || len(c.pending) != 32 || len(c.seen) != 0 {
		t.Fatal("invalid receive mutated state")
	}
	if _, err := c.receive(game.Header{Ack: 32, AckMask: 1}, now); err == nil || len(c.pending) != 32 {
		t.Fatal("future ACK")
	}
	c = channel{next: math.MaxUint32}
	if _, err := c.send([]byte{game.Acknowledge}, true, now); err != ErrWindow {
		t.Fatal("sequence wrapped")
	}
	c = channel{}
	for seq := uint32(0); seq < 300; seq++ {
		if fresh, err := c.receive(game.Header{Reliable: true, Sequence: seq}, now); err != nil || !fresh {
			t.Fatal(seq, err)
		}
	}
	if len(c.seen) != 256 {
		t.Fatal("history unbounded")
	}
	if fresh, err := c.receive(game.Header{Reliable: true, Sequence: 0}, now); err != nil || fresh {
		t.Fatal("old packet replayed")
	}
}
