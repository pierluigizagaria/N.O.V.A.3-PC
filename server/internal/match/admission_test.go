package match

import (
	"bytes"
	"net/netip"
	"testing"
	"time"

	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/rooms"
)

func lateFixture(t *testing.T) (*Bound, *rooms.Registry, uint32, time.Time) {
	t.Helper()
	now := time.Unix(100, 0)
	r := &rooms.Registry{}
	room, _ := r.Create(rooms.Player{Account: "a", ID: "a", Name: "alice"}, rooms.Settings{Capacity: 3})
	r.Join(rooms.Player{Account: "b", ID: "b", Name: "bob"}, room.ID)
	lease, _ := r.Reserve("a")
	b, err := NewMovement(lease, Options{Details: game.Details{Name: "Room", Beacon: [4]byte{0, 0, 3, 0}}, Loading: game.Loading{Capacity: 3}}, func(token string) (string, error) { return token, nil }, now)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(b.Close)
	s := b.session
	s.phase, s.epoch = Playing, now.Add(-time.Minute)
	s.round.runningSince, s.endsAt = now.Add(-10*time.Second), now.Add(time.Minute)
	s.movement.publication = newCadence(now)
	for i, p := range s.peers {
		p.joined, p.loaded, p.sceneReady = true, true, true
		if _, err := b.Issue(p.member.Account, pc.SnapshotV1, now); err != nil {
			t.Fatal(err)
		}
		b.byAccount[p.member.Account].remote = netip.AddrPortFrom(netip.MustParseAddr("127.0.0.1"), uint16(4000+i))
	}
	if !lease.Start() {
		t.Fatal("start")
	}
	return b, r, room.ID, now
}

func TestLateAdmissionPreservesExistingRoundAndScene(t *testing.T) {
	b, r, id, now := lateFixture(t)
	s := b.session
	if _, err := b.Admit("c", LoadoutPolicy{}, now); err == nil {
		t.Fatal("nonmember admitted")
	}
	if _, err := r.Join(rooms.Player{Account: "c", ID: "c", Name: "carol"}, id); err != nil {
		t.Fatal(err)
	}
	if _, err := b.Admit("c", LoadoutPolicy{}, now); err != nil {
		t.Fatal(err)
	}
	if _, err := b.Admit("c", LoadoutPolicy{}, now); err == nil {
		t.Fatal("duplicate ticket")
	}
	p := s.peers[2]
	initialRound, epoch, endsAt := s.round, s.epoch, s.endsAt
	sequence := uint32(0)
	send := func(at time.Time, body []byte, reliable bool) []Delivery {
		t.Helper()
		h := game.Header{Reliable: reliable, Sequence: sequence}
		if p.channel.next != 0 {
			h.Ack, h.AckMask = p.channel.next-1, ^uint32(0)
		}
		out, err := s.Handle(p.member.Endpoint, wire(t, h, body), at)
		if err != nil {
			t.Fatal(err)
		}
		if reliable {
			sequence++
		}
		return out
	}
	send(now, game.Discovery(), false)
	details, _ := (game.Player{Name: "carol"}).Message()
	out := send(now, details, true)
	loading := false
	for _, d := range out {
		if d.To != p.member.Endpoint {
			t.Fatal("late roster disturbed existing scene")
		}
		packet, _ := game.Decode(d.Data)
		if len(packet.Messages) == 2 && packet.Messages[0][0] == game.Roster && packet.Messages[1][0] == game.StartLoading {
			loading = packet.Messages[1][1] == 2
		}
	}
	if !loading {
		t.Fatal("missing atomic roster/loading")
	}
	lives, _, err := s.publication(now)
	declared, _ := pc.DecodeLives(lives)
	if err != nil || len(declared) != 2 {
		t.Fatal("unloaded actor published", declared, err)
	}
	send(now, []byte{game.FinishedLoading}, true)
	for i := byte(0); i < 50; i++ {
		at := now.Add(time.Duration(i) * 66 * time.Millisecond)
		if _, err := s.Tick(at); err != nil {
			t.Fatal(err)
		}
		sample, _ := game.ClockSample(i, uint32(i)*66+1000)
		send(at.Add(2*time.Millisecond), sample, false)
	}
	at := now.Add(4 * time.Second)
	s.Tick(at)
	send(at, []byte{game.Acknowledge}, false)
	out, err = s.Tick(at)
	if err != nil {
		t.Fatal(err)
	}
	started := false
	for _, d := range out {
		packet, _ := game.Decode(d.Data)
		for _, m := range packet.Messages {
			if m[0] == game.StartGame {
				if d.To != p.member.Endpoint {
					t.Fatal("existing actor restarted")
				}
				started = true
			}
		}
	}
	if !started {
		t.Fatal("newcomer did not start")
	}
	out = send(at, []byte{game.SceneReady}, true)
	announced, initialized := 0, 0
	for _, d := range out {
		packet, _ := game.Decode(d.Data)
		for _, m := range packet.Messages {
			if m[0] == game.NewPlayer {
				if d.To == p.member.Endpoint || m[1] != 2 {
					t.Fatal("duplicate/wrong actor announcement")
				}
				announced++
			}
			if m[0] == game.PlayersSnapshot {
				if d.To != p.member.Endpoint {
					t.Fatal("existing actor reinitialized")
				}
				for i := 0; i < 3; i++ {
					if m[25+i*63] != 1 {
						t.Fatal("missing initial actor")
					}
				}
				initialized++
			}
		}
	}
	if announced != 2 || initialized != 1 || s.Snapshot().Active != 3 {
		t.Fatal(announced, initialized, s.Snapshot())
	}
	send(at, []byte{game.RequestPowerups}, true)
	s.Tick(at)
	if s.round != initialRound || s.epoch != epoch || s.endsAt != endsAt {
		t.Fatal("late join reset match clocks")
	}
	for _, existing := range s.peers[:2] {
		if existing.roundSpawnPending || s.movement.poses[existing.player.Slot].Generation != 1 {
			t.Fatal("late join respawned existing actor")
		}
	}
}

func TestLateAdmissionTimeoutRetiresOnlyNewcomer(t *testing.T) {
	for _, loaded := range []bool{false, true} {
		b, r, id, now := lateFixture(t)
		r.Join(rooms.Player{Account: "c", ID: "c", Name: "carol"}, id)
		b.Admit("c", LoadoutPolicy{}, now)
		p := b.session.peers[2]
		wait := 15 * time.Second
		if loaded {
			p.admitting, p.admissionDeadline = Loading, now.Add(120*time.Second)
			wait = 120 * time.Second
			if b.session.unavailable(now.Add(40 * time.Second))[p.member.Endpoint] {
				t.Fatal("loading uses playing idle timeout")
			}
		}
		at := now.Add(wait)
		for _, other := range b.session.peers[:2] {
			other.lastReceived = at
		}
		b.session.endsAt = at.Add(time.Minute)
		if _, err := b.Tick(at); err != nil {
			t.Fatal(err)
		}
		if b.Snapshot().Phase != Playing || b.Snapshot().Active != 2 || b.byAccount["c"] != nil {
			t.Fatal("newcomer timeout stopped match")
		}
		if departures := b.DrainDepartures(); len(departures) != 1 || departures[0] != "c" {
			t.Fatal(departures)
		}
	}
}

func TestSlotReuseWaitsForQuitAcknowledgement(t *testing.T) {
	b, _, _, now := lateFixture(t)
	s := b.session
	old := s.peers[0]
	out, err := s.retire([]netip.AddrPort{old.member.Endpoint}, now)
	if err != nil || len(out) != 1 || !bytes.Equal(out[0].Data[13:], []byte{game.PlayerQuit, 0}) {
		t.Fatal(out, err)
	}
	// The fresh slot is usable while slot zero is still pending retirement.
	endpoint, err := s.admit(Member{Account: "c", Name: "carol"}, LoadoutPolicy{}, now)
	if err != nil || s.byEndpoint[endpoint].player.Slot != 2 {
		t.Fatal("premature slot reuse", err)
	}
	if _, err = s.admit(Member{Account: "d", Name: "dave"}, LoadoutPolicy{}, now); err == nil {
		t.Fatal("unacknowledged quit slot reused")
	}
	survivor := s.peers[1]
	packet, _ := game.Decode(out[0].Data)
	if _, err = s.Handle(survivor.member.Endpoint, wire(t, game.Header{Ack: packet.Header.Sequence, AckMask: 1}, []byte{game.Acknowledge}), now); err != nil {
		t.Fatal(err)
	}
	endpoint, err = s.admit(Member{Account: "d", Name: "dave"}, LoadoutPolicy{}, now)
	if err != nil || s.byEndpoint[endpoint].player.Slot != 0 || s.movement.poses[0].Generation != 2 || s.peers[1] != survivor {
		t.Fatal("stable slot/new generation missing", err)
	}
}
