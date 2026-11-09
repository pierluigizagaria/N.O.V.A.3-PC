package match

import (
	"encoding/binary"
	"errors"
	"net/netip"
	"testing"
	"time"

	"nova3.test/backend/internal/protocol/game"
)

func TestPostLoadingClockBarrier(t *testing.T) {
	now := time.Unix(100, 0)
	addr := netip.MustParseAddrPort("127.0.0.1:10001")
	s, _ := New(options(addr), now)
	// Enter at the boundary already exercised by the real two-peer UDP test.
	s.phase = Loaded
	s.peers[0].joined, s.peers[0].loaded = true, true
	for i := byte(0); i < 50; i++ {
		at := now.Add(time.Duration(i) * 66 * time.Millisecond)
		out, err := s.Tick(at)
		if i == 46 {
			// The independent native heartbeat becomes due during the probes.
			if len(out) != 2 || out[1].Data[13] != game.Ping {
				t.Fatal("missing heartbeat during synchronization", out, err)
			}
			out = out[:1]
		}
		if err != nil || len(out) != 1 || out[0].Data[13] != game.RequestClock || out[0].Data[14] != i {
			t.Fatal(i, out, err)
		}
		if i == 3 {
			continue
		} // Lost samples do not stall subsequent probes.
		body, _ := game.ClockSample(i, uint32(i)*66+1001)
		if _, err := s.Handle(addr, wire(t, game.Header{}, body), at.Add(2*time.Millisecond)); err != nil {
			t.Fatal(err)
		}
		// A duplicate with a different value must not replace a measured sample.
		body, _ = game.ClockSample(i, 999999)
		s.Handle(addr, wire(t, game.Header{}, body), at.Add(3*time.Millisecond))
	}
	out, err := s.Tick(now.Add(4 * time.Second))
	if err != nil || len(out) != 1 {
		t.Fatal(out, err)
	}
	pkt, _ := game.Decode(out[0].Data)
	msg := pkt.Messages[0]
	if msg[0] != game.SetClock || binary.BigEndian.Uint32(msg[1:]) != 2 || int32(binary.BigEndian.Uint32(msg[5:])) != -1000 || !pkt.Header.Reliable {
		t.Fatal("clock correction", msg, pkt.Header)
	}
	if s.Snapshot().Phase != Synchronizing {
		t.Fatal("no unacknowledged start")
	}
	s.Handle(addr, wire(t, game.Header{Ack: pkt.Header.Sequence, AckMask: 1}, []byte{game.Acknowledge}), now.Add(4*time.Second))
	out, err = s.Tick(now.Add(4 * time.Second))
	if err != nil || len(out) != 0 || s.Snapshot().Phase != Synchronized {
		t.Fatal("clock barrier", out, err, s.Snapshot())
	}
	if _, err = s.Tick(now.Add(19 * time.Second)); !errors.Is(err, ErrTimeout) {
		t.Fatal("no fabricated gameplay after sync", err)
	}
}

func TestClockRejectsUnissuedSamplesAndTimesOutWithoutEvidence(t *testing.T) {
	now := time.Unix(100, 0)
	addr := netip.MustParseAddrPort("127.0.0.1:10001")
	s, _ := New(options(addr), now)
	s.phase = Loaded
	s.peers[0].joined = true
	s.Tick(now)
	before := s.peers[0].clock
	body, _ := game.ClockSample(1, 123)
	if _, err := s.Handle(addr, wire(t, game.Header{}, body), now.Add(time.Millisecond)); !errors.Is(err, ErrState) || s.peers[0].clock != before {
		t.Fatal("unissued sample changed state", err)
	}
	for i := 1; i < 50; i++ {
		s.Tick(now.Add(time.Duration(i) * 66 * time.Millisecond))
	}
	if out, err := s.Tick(now.Add(5 * time.Second)); err != nil || len(out) != 0 {
		t.Fatal("fabricated correction", out, err)
	}
	if _, err := s.Tick(now.Add(15 * time.Second)); !errors.Is(err, ErrTimeout) {
		t.Fatal(err)
	}
}

func TestNativeClockWrap(t *testing.T) {
	c := clockSync{next: 1}
	c.samples[0].sent = 100
	c.receive(0, 0xfffffff0, 104)
	if c.offset != 118 || c.bestRTT != 4 {
		t.Fatal("native modulo word semantics", c)
	}
}
