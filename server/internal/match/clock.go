package match

import (
	"time"

	"nova3.test/backend/internal/protocol/game"
)

type clockSample struct {
	sent     uint32
	received bool
}
type clockSync struct {
	samples             [50]clockSample
	next                byte
	last                time.Time
	bestRTT             uint32
	offset              int32
	haveBest, corrected bool
}

func (c *clockSync) validSample(index byte) bool { return index < c.next }

func (c *clockSync) receive(index byte, clientClock, now uint32) {
	sample := &c.samples[index]
	if sample.received || c.corrected {
		return
	}
	sample.received = true
	rtt := now - sample.sent
	// Original synchronization picks the smallest positive RTT from 50 samples.
	// Millisecond-zero responses carry no usable timing evidence. The session
	// deadline bounds the unsigned subtraction and prevents clock-wrap ambiguity.
	if rtt == 0 || rtt > 15000 || (c.haveBest && rtt >= c.bestRTT) {
		return
	}
	c.haveBest, c.bestRTT = true, rtt
	c.offset = int32(sample.sent + rtt/2 - clientClock)
}

func (s *Session) elapsed(now time.Time) uint32 { return uint32(now.Sub(s.epoch).Milliseconds()) }

func (s *Session) synchronize(now time.Time, out *[]Delivery) error {
	if s.phase == Loaded {
		s.phase = Synchronizing
		s.epoch = now
		s.hitHistory = nil // No collision frame may survive a change of clock epoch.
		s.deadline = now.Add(15 * time.Second)
	}
	if s.phase != Synchronizing {
		return nil
	}
	all := true
	for _, p := range s.peers {
		if err := s.synchronizePeer(p, now, out); err != nil {
			return err
		}
		c := &p.clock
		if !c.corrected || len(p.channel.pending) != 0 {
			all = false
		}
	}
	if all {
		s.phase = Synchronized
		s.deadline = now.Add(15 * time.Second)
	}
	// The synchronization component does not choose spawns. An attached movement
	// session supplies verified map spawn IDs and a common seed after this gate.
	return nil
}

// Individual clock probes use the existing match epoch, including late joins.
func (s *Session) synchronizePeer(p *peer, now time.Time, out *[]Delivery) error {
	c := &p.clock
	if c.next < 50 && (c.last.IsZero() || now.Sub(c.last) > 65*time.Millisecond) {
		if err := s.emit(p, []byte{game.RequestClock, c.next}, false, now, out); err != nil {
			return err
		}
		c.samples[c.next].sent = s.elapsed(now)
		c.next++
		c.last = now
	}
	// The native code leaves twenty update passes after the last probe.
	// Use a bounded elapsed-time grace in the independent server; packet
	// semantics and >65ms probe spacing are unchanged by scheduler frequency.
	if c.next == 50 && !c.corrected && now.Sub(c.last) >= 500*time.Millisecond && c.haveBest {
		if err := s.emit(p, game.ClockCorrection(c.bestRTT, c.offset), true, now, out); err != nil {
			return err
		}
		c.corrected = true
	}
	return nil
}
