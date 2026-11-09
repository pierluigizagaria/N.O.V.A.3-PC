package match

import (
	"bytes"
	"errors"
	"net/netip"
	"testing"
	"time"

	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/protocol/secure"
)

func TestDepartedOwnerCannotStopOrRejoinRunningServer(t *testing.T) {
	b := movementFixture(t, 0)
	now := time.Unix(100, 0)
	s := b.session
	s.phase, s.deadline, s.epoch = Playing, now.Add(time.Minute), now
	s.movement.publication = newCadence(now)
	for _, p := range s.peers {
		p.joined, p.loaded, p.sceneReady = true, true, true
	}
	if !b.reservation.Start() {
		t.Fatal("reservation start")
	}
	tickets := make([]secure.Ticket, 2)
	for i, account := range []string{"a", "b"} {
		var err error
		tickets[i], err = b.Issue(account, pc.SnapshotV1, now)
		if err != nil {
			t.Fatal(err)
		}
		b.byAccount[account].remote = netip.AddrPortFrom(netip.MustParseAddr("127.0.0.1"), uint16(4000+i))
	}
	b.resolve = func(token string) (string, error) {
		if token == "a" {
			return "", ErrAuthorization
		}
		return token, nil
	}
	out, err := b.Tick(now.Add(time.Second))
	if err != nil || b.Snapshot().Phase != Playing || b.Snapshot().Reserved != 1 || b.Snapshot().Active != 1 {
		t.Fatal(b.Snapshot(), err)
	}
	if b.byAccount["a"] != nil {
		t.Fatal("revoked ticket retained")
	}
	channel, _ := secure.New(tickets[1], false)
	quit, pose := false, false
	for _, d := range out {
		if d.To.Port() != 4001 {
			t.Fatal("delivery to departed client")
		}
		plain, err := channel.Open(d.Data)
		if err != nil {
			t.Fatal(err)
		}
		kind, body, _ := pc.Decode(plain)
		if kind == pc.Native {
			packet, _ := game.Decode(body)
			for _, m := range packet.Messages {
				if bytes.Equal(m, []byte{game.PlayerQuit, 0}) {
					quit = packet.Header.Reliable
				}
			}
		}
		if kind == pc.Snapshots {
			records, _ := pc.DecodeSnapshots(body)
			pose = len(records) == 1 && records[0].Slot == 1 && records[0].Generation == 1
		}
	}
	if !quit || !pose {
		t.Fatal("missing reliable departure or sparse publication", quit, pose)
	}
	if _, err = b.Issue("a", pc.SnapshotV1, now); !errors.Is(err, ErrAuthorization) {
		t.Fatal("revoked participant readmitted")
	}
	old, _ := secure.New(tickets[0], false)
	payload, _ := pc.Encode(pc.Native, wire(t, game.Header{}, []byte{game.Acknowledge}))
	sealed, _ := old.Seal(payload)
	if _, err = b.Handle(netip.MustParseAddrPort("127.0.0.1:4000"), sealed, now); !errors.Is(err, ErrAuthorization) {
		t.Fatal("departed ticket admitted", err)
	}
	b.resolve = func(string) (string, error) { return "", ErrAuthorization }
	if _, err = b.Tick(now.Add(2 * time.Second)); !errors.Is(err, ErrAuthorization) || b.Snapshot().Phase != Closed {
		t.Fatal("empty session retained", err)
	}
}

func TestStalledReceiverCannotTerminateOtherPlayers(t *testing.T) {
	for _, reason := range []string{"idle", "unacknowledged", "window"} {
		t.Run(reason, func(t *testing.T) {
			b := movementFixture(t, 0)
			now := time.Unix(100, 0)
			s := b.session
			s.phase, s.deadline, s.epoch = Playing, now.Add(time.Minute), now
			s.movement.publication = newCadence(now)
			if !b.reservation.Start() {
				t.Fatal("start")
			}
			for i, account := range []string{"a", "b"} {
				p := s.peers[i]
				p.joined, p.loaded, p.sceneReady = true, true, true
				p.lastReceived = now
				if _, err := b.Issue(account, pc.SnapshotV1, now); err != nil {
					t.Fatal(err)
				}
				b.byAccount[account].remote = netip.AddrPortFrom(netip.MustParseAddr("127.0.0.1"), uint16(4000+i))
			}
			stalled := s.peers[0]
			tick := now.Add(peerTimeout)
			s.peers[1].lastReceived = tick
			switch reason {
			case "unacknowledged":
				stalled.lastReceived = tick // Valid movement cannot hide missing scene ACKs.
				if _, err := stalled.channel.send([]byte{game.Acknowledge}, true, now); err != nil {
					t.Fatal(err)
				}
			case "window":
				tick = now.Add(time.Second)
				for range 32 {
					if _, err := stalled.channel.send([]byte{game.Acknowledge}, true, now); err != nil {
						t.Fatal(err)
					}
				}
				var out []Delivery
				if err := s.emit(stalled, []byte{game.Acknowledge}, true, now, &out); err != nil || len(out) != 0 || s.phase != Playing {
					t.Fatal("one receiver failed the match", err)
				}
			}
			if _, err := b.Tick(tick); err != nil {
				t.Fatal(err)
			}
			if b.Snapshot().Phase != Playing || b.Snapshot().Active != 1 || b.byAccount["a"] != nil || b.byAccount["b"] == nil {
				t.Fatal("survivor removed", b.Snapshot())
			}
			removed := b.DrainDepartures()
			if len(removed) != 1 || removed[0] != "a" || len(b.DrainDepartures()) != 0 {
				t.Fatal("departure notification", removed)
			}
			if _, err := b.Issue("a", pc.SnapshotV1, tick); !errors.Is(err, ErrAuthorization) {
				t.Fatal("stalled ticket replaced", err)
			}
		})
	}
}

func TestOnlyAcceptedTrafficRefreshesPeerTimeout(t *testing.T) {
	b := movementFixture(t, 0)
	s := b.session
	now := time.Unix(100, 0)
	s.phase, s.deadline, s.epoch = Playing, now.Add(time.Minute), now
	p := s.peers[0]
	p.joined, p.loaded = true, true
	if _, err := s.Handle(p.member.Endpoint, wire(t, game.Header{}, []byte{game.Acknowledge}), now.Add(20*time.Second)); err != nil {
		t.Fatal(err)
	}
	if s.unavailable(now.Add(31 * time.Second))[p.member.Endpoint] {
		t.Fatal("accepted native traffic did not refresh timeout")
	}
	pose := s.movement.poses[0]
	pose.Time, pose.Sequence = 40000, 1
	if err := s.propose(p.member.Endpoint, []pc.Snapshot{pose}, now.Add(40*time.Second)); err != nil {
		t.Fatal(err)
	}
	pose.Slot = 1
	if err := s.propose(p.member.Endpoint, []pc.Snapshot{pose}, now.Add(69*time.Second)); err == nil {
		t.Fatal("spoofed pose")
	}
	if !s.unavailable(now.Add(70 * time.Second))[p.member.Endpoint] {
		t.Fatal("invalid traffic kept an endpoint alive")
	}
}
