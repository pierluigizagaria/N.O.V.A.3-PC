package match

import (
	"bytes"
	"context"
	"errors"
	"net"
	"net/netip"
	"testing"
	"time"

	"nova3.test/backend/internal/protocol/game"
)

func options(endpoints ...netip.AddrPort) Options {
	v := Options{Details: game.Details{Name: "Room", Beacon: [4]byte{2, 0, 2, 1}}, Loading: game.Loading{Map: 2, Mode: 1, Capacity: 2, Parameter1: 300, Parameter2: 600}}
	for i, addr := range endpoints {
		name := string(rune('A' + i))
		v.Members = append(v.Members, Member{Account: name, Name: name, Endpoint: addr})
	}
	return v
}

func wire(t *testing.T, h game.Header, messages ...[]byte) []byte {
	t.Helper()
	b, err := game.Encode(h, messages...)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func TestSessionAdmissionLoadingAndLoss(t *testing.T) {
	now := time.Unix(100, 0)
	a, b := netip.MustParseAddrPort("127.0.0.1:10001"), netip.MustParseAddrPort("127.0.0.1:10002")
	configuration := options(a, b)
	configuration.Loading.Mode, configuration.Details.Beacon[3] = 0, 0
	s, err := New(configuration, now)
	if err != nil {
		t.Fatal(err)
	}
	if err := s.RequestLoading(); !errors.Is(err, ErrState) {
		t.Fatal("premature loading", err)
	}
	for i, addr := range []netip.AddrPort{a, b} {
		out, err := s.Handle(addr, wire(t, game.Header{}, game.Discovery()), now)
		if err != nil || len(out) != 1 || out[0].Data[13] != game.ServerDetails {
			t.Fatal("discovery", out, err)
		}
		body, _ := (game.Player{Name: string(rune('A' + i))}).Message()
		out, err = s.Handle(addr, wire(t, game.Header{Reliable: true}, body), now)
		if err != nil || len(out) != i+2 {
			t.Fatal("join", len(out), err)
		}
		if s.peers[i].player.Beacon[0] != byte(i+1) || s.peers[i].admission.Beacon[0] != 0 {
			t.Fatal("FFA individual team must be server-assigned without changing the admission identity")
		}
	}
	if err := s.RequestLoading(); err != nil {
		t.Fatal(err)
	}
	// The real client withholds idle ACKs until it has two reliable packets.
	// Drop/withhold all earlier roster ACKs: loading must still arrive with its
	// own complete roster, rather than waiting on a circular ACK dependency.
	out, err := s.Tick(now)
	if err != nil || len(out) != 2 || s.Snapshot().Phase != Loading {
		t.Fatal("loading transition", out, err)
	}
	retried, err := s.Tick(now.Add(time.Second))
	if err != nil || len(retried) != 5 {
		t.Fatal("lost rosters and loading bundles must remain reliable", len(retried), err)
	}
	for _, sent := range out {
		found := false
		for _, retry := range retried {
			found = found || (sent.To == retry.To && bytes.Equal(sent.Data, retry.Data))
		}
		if !found {
			t.Fatal("loading retry must preserve roster-before-loading order")
		}
	}
	for i, d := range out {
		p, err := game.Decode(d.Data)
		if err != nil || !p.Header.Reliable || len(p.Messages) != 2 || p.Messages[0][0] != game.Roster || p.Messages[0][12] != 2 || p.Messages[1][0] != game.StartLoading || p.Messages[1][1] != byte(i) {
			t.Fatal("per-player loading", err)
		}
		h := game.Header{Reliable: true, Sequence: 1, Ack: p.Header.Sequence, AckMask: (1 << (p.Header.Sequence + 1)) - 1}
		packet := wire(t, h, []byte{game.FinishedLoading})
		ack, err := s.Handle(d.To, packet, now.Add(time.Second))
		if err != nil || len(ack) != 1 || ack[0].Data[13] != game.Acknowledge {
			t.Fatal("finished", err)
		}
		if _, err = s.Handle(d.To, packet, now.Add(time.Second)); err != nil {
			t.Fatal("duplicate completion", err)
		}
	}
	if got := s.Snapshot(); got.Phase != Loaded || got.Joined != 2 || got.Loaded != 2 || got.Pending != 0 {
		t.Fatal(got)
	}
	if _, err := s.Tick(now.Add(16 * time.Second)); !errors.Is(err, ErrTimeout) {
		t.Fatal("loaded state cannot pretend to run gameplay", err)
	}
	s.Close()
	if s.Snapshot().Phase != Failed {
		t.Fatal("preserve failure")
	}
}

func TestSessionRejectsWithoutPartialMutation(t *testing.T) {
	now := time.Unix(100, 0)
	addr := netip.MustParseAddrPort("127.0.0.1:10001")
	s, _ := New(options(addr), now)
	discover := wire(t, game.Header{}, game.Discovery())
	if _, err := s.Handle(netip.MustParseAddrPort("127.0.0.1:10002"), discover, now); !errors.Is(err, ErrPeer) {
		t.Fatal(err)
	}
	join, _ := (game.Player{Name: "A"}).Message()
	if _, err := s.Handle(addr, wire(t, game.Header{Reliable: true}, join), now); !errors.Is(err, ErrPeer) {
		t.Fatal("undiscovered", err)
	}
	s.Handle(addr, discover, now)
	if _, err := s.Handle(addr, wire(t, game.Header{Reliable: true}, join), now); err != nil {
		t.Fatal(err)
	}
	before := s.Snapshot()
	for _, bad := range [][]byte{
		wire(t, game.Header{AckMask: 1}, []byte{game.Acknowledge}, []byte{game.FinishedLoading}),
		append(wire(t, game.Header{AckMask: 1}, []byte{game.Acknowledge}), 255),
		wire(t, game.Header{Ack: 10, AckMask: 1}, []byte{game.Acknowledge}),
	} {
		if _, err := s.Handle(addr, bad, now); err == nil {
			t.Fatal("accepted bad datagram")
		}
		if s.Snapshot() != before || len(s.peers[0].channel.seen) != 1 {
			t.Fatal("partially applied rejection")
		}
	}
	for _, p := range []game.Player{{Name: "B"}, {Name: "A", Beacon: [16]byte{1}}} {
		body, _ := p.Message()
		if _, err := s.Handle(addr, wire(t, game.Header{Reliable: true, Sequence: 1}, body), now); !errors.Is(err, ErrPeer) {
			t.Fatal("changed identity", err)
		}
	}
	if _, err := s.Handle(addr, wire(t, game.Header{Reliable: true}, join), now); err != nil || s.Snapshot() != before {
		t.Fatal("duplicate join reapplied", err)
	}
	s.Close()
	if v := s.Snapshot(); v.Phase != Closed || v.Pending != 0 {
		t.Fatal(v)
	}
	if _, err := s.Tick(now); !errors.Is(err, ErrState) {
		t.Fatal(err)
	}
	for _, modify := range []func(*Options){
		func(v *Options) { v.Members = nil },
		func(v *Options) { v.Members = append(v.Members, v.Members[0]) },
		func(v *Options) { v.Members[0].Endpoint = netip.MustParseAddrPort("0.0.0.0:1") },
		func(v *Options) { v.Details.Beacon[0]++ },
	} {
		v := options(addr)
		modify(&v)
		if _, err := New(v, now); err == nil {
			t.Fatal("invalid reservation")
		}
	}
	s, _ = New(options(addr), now)
	if _, err := s.Tick(now.Add(15 * time.Second)); !errors.Is(err, ErrTimeout) {
		t.Fatal("admission deadline", err)
	}
}

func listen(t *testing.T) *net.UDPConn {
	t.Helper()
	c, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close() })
	return c
}

func TestUDPMultiplePeersAndCancellation(t *testing.T) {
	host, a, b := listen(t), listen(t), listen(t)
	addr := host.LocalAddr().(*net.UDPAddr).AddrPort()
	s, err := New(options(a.LocalAddr().(*net.UDPAddr).AddrPort(), b.LocalAddr().(*net.UDPAddr).AddrPort()), time.Now())
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- Run(ctx, host, s) }()
	send := func(c *net.UDPConn, data []byte) {
		t.Helper()
		if _, err := c.WriteToUDPAddrPort(data, addr); err != nil {
			t.Fatal(err)
		}
	}
	read := func(c *net.UDPConn, kind byte) game.Packet {
		t.Helper()
		c.SetReadDeadline(time.Now().Add(3 * time.Second))
		for {
			buf := make([]byte, 2048)
			n, _, err := c.ReadFromUDPAddrPort(buf)
			if err != nil {
				t.Fatal(err)
			}
			p, err := game.Decode(buf[:n])
			if err != nil {
				t.Fatal(err)
			}
			for _, message := range p.Messages {
				if message[0] == kind {
					return p
				}
			}
		}
	}
	// Neither a malformed nor an oversized packet may kill the socket on Windows.
	send(a, bytes.Repeat([]byte{255}, 4096))
	for i, c := range []*net.UDPConn{a, b} {
		send(c, wire(t, game.Header{}, game.Discovery()))
		read(c, game.ServerDetails)
		body, _ := (game.Player{Name: string(rune('A' + i))}).Message()
		send(c, wire(t, game.Header{Reliable: true}, body))
		read(c, game.Roster)
	}
	// Peer A has its second roster queued. Receiving it proves both admissions.
	read(a, game.Roster)
	if err := s.RequestLoading(); err != nil {
		t.Fatal(err)
	}
	send(a, wire(t, game.Header{Ack: 1, AckMask: 3}, []byte{game.Acknowledge}))
	send(b, wire(t, game.Header{AckMask: 1}, []byte{game.Acknowledge}))
	for _, c := range []*net.UDPConn{a, b} {
		p := read(c, game.StartLoading)
		send(c, wire(t, game.Header{Reliable: true, Sequence: 1, Ack: p.Header.Sequence, AckMask: 1}, []byte{game.FinishedLoading}))
		for ack := read(c, game.Acknowledge); ack.Header.Ack != 1; ack = read(c, game.Acknowledge) {
		}
	}
	if got := s.Snapshot(); (got.Phase != Loaded && got.Phase != Synchronizing) || got.Loaded != 2 {
		t.Fatal(got)
	}
	cancel()
	select {
	case err := <-done:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("cancellation did not release UDP")
	}
	if s.Snapshot().Phase != Closed {
		t.Fatal("session not closed")
	}
	if _, err := host.WriteToUDPAddrPort([]byte{1}, addr); !errors.Is(err, net.ErrClosed) {
		t.Fatal("socket ownership", err)
	}
}
