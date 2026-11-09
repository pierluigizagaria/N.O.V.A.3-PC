package match

import (
	"encoding/binary"
	"net/netip"
	"testing"
	"time"

	"nova3.test/backend/internal/protocol/game"
)

func TestHostHeartbeatSurvivesLossAndDoesNotEchoReplies(t *testing.T) {
	now := time.Unix(100, 0)
	a := netip.MustParseAddrPort("127.0.0.1:10001")
	b := netip.MustParseAddrPort("127.0.0.1:10002")
	s, err := New(options(a, b), now)
	if err != nil {
		t.Fatal(err)
	}
	s.phase, s.deadline = Playing, now.Add(time.Minute)
	for _, p := range s.peers {
		p.joined = true
	}
	// Advance simulated time well past the native 30-second idle deadline.
	for second := 0; second < 50; second++ {
		at := now.Add(time.Duration(second) * time.Second)
		out, err := s.Tick(at)
		if err != nil {
			t.Fatal(err)
		}
		due := second > 0 && second%3 == 0
		if (!due && len(out) != 0) || (due && len(out) != 2) {
			t.Fatalf("second %d: %v", second, out)
		}
		for i, d := range out {
			pkt, err := game.Decode(d.Data)
			if err != nil || pkt.Header.Reliable || len(pkt.Messages) != 1 || pkt.Messages[0][0] != game.Ping {
				t.Fatal(pkt, err)
			}
			if binary.BigEndian.Uint32(pkt.Messages[0][1:]) != uint32(second/3) {
				t.Fatal("ping sequence", pkt)
			}
			if i == 1 {
				continue
			} // The second client's missing answers must not stall either peer.
			wantRTT := uint32(25)
			if second == 3 {
				wantRTT = 0
			}
			if binary.BigEndian.Uint32(pkt.Messages[0][5:]) != wantRTT {
				t.Fatal("measured RTT", pkt)
			}
			reply := append([]byte(nil), pkt.Messages[0]...)
			clear(reply[5:])
			for _, delay := range []time.Duration{25 * time.Millisecond, 100 * time.Millisecond} {
				out, err := s.Handle(d.To, wire(t, game.Header{}, reply), at.Add(delay))
				if err != nil || len(out) != 0 || s.peers[i].ping.rtt != 25 {
					t.Fatal("echo loop or duplicate RTT update", out, err)
				}
			}
		}
	}
	if s.Snapshot().Phase != Playing {
		t.Fatal(s.Snapshot())
	}
}

func TestHeartbeatIgnoresUnissuedAndOldRepliesAndDoesNotBurst(t *testing.T) {
	now := time.Unix(100, 0)
	h := heartbeat{}
	h.receive(0, now)
	if h.rtt != 0 || h.pending {
		t.Fatal(h)
	}
	h.poll(now)
	h.poll(now.Add(3 * time.Second))
	h.poll(now.Add(20 * time.Second))
	h.receive(1, now.Add(20*time.Second+time.Millisecond))
	if !h.pending || h.rtt != 0 {
		t.Fatal("old reply accepted", h)
	}
	if h.poll(now.Add(20*time.Second)) != nil {
		t.Fatal("catch-up burst")
	}
	h.receive(2, now.Add(20*time.Second+7*time.Millisecond))
	if h.pending || h.rtt != 7 {
		t.Fatal(h)
	}
}
