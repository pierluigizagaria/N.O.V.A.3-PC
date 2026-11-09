package match

import (
	"bytes"
	"errors"
	"math"
	"time"

	"nova3.test/backend/internal/protocol/game"
)

var ErrWindow = errors.New("game reliability window exhausted")

type pending struct {
	sequence    uint32
	data        []byte
	first, last time.Time
	retried     bool
}

// channel is owned by a session's mutex. The original mask includes Ack at bit
// zero and its sequence comparison is unsigned, not RFC1982 serial arithmetic.
// No wrap semantics are invented; allocation of sequence MaxUint32 is refused.
type channel struct {
	next, received uint32
	haveReceived   bool
	seen           map[uint32]struct{}
	pending        []pending
	rtt            time.Duration
}

func (c *channel) header(reliable bool) game.Header {
	h := game.Header{Reliable: reliable, Sequence: c.next, Ack: c.received}
	for seq := range c.seen {
		if seq <= c.received && c.received-seq < 32 {
			h.AckMask |= 1 << (c.received - seq)
		}
	}
	return h
}

func (c *channel) send(body []byte, reliable bool, now time.Time) ([]byte, error) {
	if reliable && (len(c.pending) >= 32 || c.next == math.MaxUint32) {
		return nil, ErrWindow
	}
	b, err := game.Encode(c.header(reliable), body)
	if err != nil {
		return nil, err
	}
	if reliable {
		c.pending = append(c.pending, pending{sequence: c.next, data: bytes.Clone(b), first: now, last: now})
		c.next++
	}
	return b, nil
}

// receive requires the complete datagram and its session-level messages to have
// been validated first. Duplicates may acknowledge our queue but never reapply
// a player join or loading completion. The bounded history matches the native
// 256-record retention; ACK publication only represents the latest 32 records.
func (c *channel) receive(h game.Header, now time.Time) (bool, error) {
	if h.AckMask != 0 && (c.next == 0 || h.Ack >= c.next) {
		return false, game.ErrPacket
	}
	if h.Reliable {
		if h.Sequence == math.MaxUint32 || (!c.haveReceived && h.Sequence > 31) ||
			(c.haveReceived && h.Sequence > c.received && h.Sequence-c.received > 256) {
			return false, game.ErrPacket
		}
	}
	kept := c.pending[:0]
	for _, p := range c.pending {
		if p.sequence <= h.Ack && h.Ack-p.sequence < 32 && h.AckMask&(1<<(h.Ack-p.sequence)) != 0 {
			if !p.retried && !now.Before(p.first) {
				c.rtt = min(now.Sub(p.first), 2*time.Second)
			}
		} else {
			kept = append(kept, p)
		}
	}
	clear(c.pending[len(kept):])
	c.pending = kept
	if !h.Reliable {
		return true, nil
	}
	if c.haveReceived && h.Sequence <= c.received && c.received-h.Sequence >= 256 {
		return false, nil
	}
	if _, exists := c.seen[h.Sequence]; exists {
		return false, nil
	}
	if c.seen == nil {
		c.seen = make(map[uint32]struct{})
	}
	if !c.haveReceived || h.Sequence > c.received {
		c.received = h.Sequence
		c.haveReceived = true
	}
	for seq := range c.seen {
		if c.received-seq >= 256 {
			delete(c.seen, seq)
		}
	}
	c.seen[h.Sequence] = struct{}{}
	return true, nil
}

func (c *channel) retries(now time.Time) [][]byte {
	var out [][]byte
	for i := range c.pending {
		p := &c.pending[i]
		// Original SendMessageFromQueue waits strictly longer than RTT + 198ms
		// and retransmits the stored packet, including its original header.
		if now.Sub(p.last) > c.rtt+198*time.Millisecond {
			out = append(out, bytes.Clone(p.data))
			p.last = now
			p.retried = true
		}
	}
	return out
}
