package match

import (
	"time"

	"nova3.test/backend/internal/protocol/game"
)

// EndGame is an original server command, not a client-controlled result. Keep
// tickets alive while its reliable packet drains, then release the allocation
// so the existing room can be rejoined and started again. Combat/scoring rules
// are separate; this path does not invent wins or upload client scores.
func (s *Session) endGame(now time.Time) ([]Delivery, error) {
	s.resultTime = now
	s.phase, s.deadline = Finishing, now.Add(peerTimeout)
	s.endGrace = now.Add(2 * time.Second)
	var out []Delivery
	for _, p := range s.peers {
		if p.joined {
			if err := s.emit(p, []byte{game.EndGame}, true, now, &out); err != nil {
				return nil, err
			}
		}
	}
	return out, nil
}

func (s *Session) receiveEnding(p *peer, packet game.Packet, now time.Time) ([]Delivery, error) {
	// The complete packet already passed structural decoding. Late gameplay
	// commands are deliberately ignored after the end barrier; their ACK header
	// may acknowledge EndGame. No command can restart or alter the finished game.
	if _, err := p.channel.receive(packet.Header, now); err != nil {
		return nil, err
	}
	p.lastReceived = now
	var out []Delivery
	if packet.Header.Reliable {
		if err := s.emit(p, []byte{game.Acknowledge}, false, now, &out); err != nil {
			return nil, err
		}
	}
	return out, nil
}

func (s *Session) tickEnding(now time.Time) []Delivery {
	if s.phase == Finished {
		return nil
	}
	all := true
	for _, p := range s.peers {
		if !p.departed && !p.transportFailed && len(p.channel.pending) != 0 {
			all = false
		}
	}
	if (all && !now.Before(s.endGrace)) || !now.Before(s.deadline) {
		s.phase = Finished
		return nil
	}
	var out []Delivery
	for _, p := range s.peers {
		if p.departed || p.transportFailed {
			continue
		}
		for _, data := range p.channel.retries(now) {
			out = append(out, Delivery{p.member.Endpoint, data})
		}
	}
	return out
}
