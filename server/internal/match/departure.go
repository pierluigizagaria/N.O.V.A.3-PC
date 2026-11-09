package match

import (
	"net/netip"
	"time"

	"nova3.test/backend/internal/protocol/game"
)

// Match transport timeout follows the original Comms idle interval. Even if
// movement continues, a reliable command unacknowledged for that long means
// the recipient cannot maintain the native scene's ordered state.
const peerTimeout = 30 * time.Second

func (s *Session) unavailable(now time.Time) map[netip.AddrPort]bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	failed := make(map[netip.AddrPort]bool)
	if s.phase != Playing || s.movement == nil {
		return failed
	}
	for _, p := range s.peers {
		if p.departed {
			continue
		}
		if p.admitting != "" && !p.sceneReady {
			// Loading can legitimately silence UDP for longer than the playing
			// idle timeout. Bound this admission without retiring other actors.
			failed[p.member.Endpoint] = p.transportFailed || !now.Before(p.admissionDeadline)
			continue
		}
		stalled := len(p.channel.pending) != 0 && now.Sub(p.channel.pending[0].first) >= peerTimeout
		if p.transportFailed || now.Sub(p.lastReceived) >= peerTimeout || stalled {
			failed[p.member.Endpoint] = true
		}
	}
	return failed
}

// retire removes all departed endpoints before generating notifications. Slots
// and life generations are never renumbered, even when slot zero leaves.
func (s *Session) retire(endpoints []netip.AddrPort, now time.Time) ([]Delivery, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if (s.phase != Playing && s.phase != Finishing) || s.movement == nil {
		return nil, ErrState
	}
	var body []byte
	var out []Delivery
	for _, endpoint := range endpoints {
		p := s.byEndpoint[endpoint]
		if p == nil {
			continue
		}
		if err := s.dropFlag(p, now, &out); err != nil {
			return nil, err
		}
		// Exit must precede PlayerQuit: the native gun controller owns the
		// reservation and disables its interaction buttons on every client.
		// Clearing only our seat leaves that controller reserved remotely.
		if err := s.leaveMount(p, now, &out); err != nil {
			return nil, err
		}
		delete(s.byEndpoint, endpoint)
		p.life.playedTo, p.life.playStopped = s.roundClock(now).Elapsed, true
		p.departed = true
		p.sceneReady, p.joined, p.loaded = false, false, false
		p.channel = channel{}
		if s.phase == Playing && p.admitting != Accepting && p.admitting != Loading && p.admitting != Synchronizing {
			body = append(body, game.PlayerQuit, p.player.Slot)
		}
	}
	if len(body) == 0 {
		return out, nil
	}
	for _, p := range s.peers {
		if !p.departed && p.sceneReady {
			if err := s.emit(p, body, true, now, &out); err != nil {
				return nil, err
			}
		}
	}
	return out, nil
}
