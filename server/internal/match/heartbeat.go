package match

import (
	"encoding/binary"
	"time"

	"nova3.test/backend/internal/protocol/game"
)

// Native Comms::UpdatePing is host-only. PC snapshots do not enter Comms and
// cannot refresh its 30-second idle timer. Keep the original type-13 exchange
// alive independently of movement, reliable retries and clock synchronization.
const pingInterval = 3 * time.Second

type heartbeat struct {
	next, sent    time.Time
	sequence, rtt uint32
	pending       bool
	confirmed     bool
}

func (h *heartbeat) poll(now time.Time) []byte {
	if h.next.IsZero() {
		h.next = now.Add(pingInterval)
	}
	if now.Before(h.next) {
		return nil
	}
	// A lost ping never blocks the next one; a delayed tick sends only once.
	h.next, h.sent = now.Add(pingInterval), now
	h.sequence++
	h.pending = true
	body := make([]byte, 9)
	body[0] = game.Ping
	binary.BigEndian.PutUint32(body[1:], h.sequence)
	binary.BigEndian.PutUint32(body[5:], h.rtt)
	return body
}

func (h *heartbeat) receive(sequence uint32, now time.Time) {
	// The client echoes the sequence and clears the RTT word. The host consumes
	// that answer; echoing it again would create an endless request/reply loop.
	if !h.pending || sequence != h.sequence || now.Before(h.sent) {
		return
	}
	h.rtt = uint32(now.Sub(h.sent).Milliseconds())
	h.pending = false
	h.confirmed = true
}
