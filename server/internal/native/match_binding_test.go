package native

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"net"
	"net/http/httptest"
	"net/url"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/match"
	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/protocol/lobby"
	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/protocol/secure"
)

func TestPasswordCredentialBindsFrozenLobbyToEncryptedUDP(t *testing.T) {
	s, alice, _, _ := fixture(t)
	bob, err := accounts.Create(s.Store, "bob")
	if err != nil {
		t.Fatal(err)
	}
	if err = grants.Provision(s.Store, bob.ID, s.Shop.Catalogue.Version); err != nil {
		t.Fatal(err)
	}
	if err = accounts.SetPassword(s.Store, bob.ID, "bob-test-password"); err != nil {
		t.Fatal(err)
	}
	a, b := connectRoomClient(t, s, alice), connectRoomClient(t, s, bob.ID)
	created := a.exchange(t, lobby.CreateRoomRequest, createRoomPayload(a.id), lobby.CreateRoomResponse)
	id := binary.BigEndian.Uint32(created[4:])
	b.exchange(t, lobby.JoinRoomRequest, append(append(lobby.Integers(id), 0), lobby.String("bob-data")...), lobby.JoinRoomResponse)
	a.read(t, lobby.MemberJoined)
	reservation, err := s.rooms.registry.Reserve(alice)
	if err != nil {
		t.Fatal(err)
	}
	bound, err := match.NewBound(reservation, match.Options{Details: game.Details{Name: "Room", Beacon: [4]byte{0, 0, 2, 0}}, Loading: game.Loading{Capacity: 2}}, func(token string) (string, error) { return accounts.Resolve(s.Store, token) }, time.Now())
	if err != nil {
		reservation.Release()
		t.Fatal(err)
	}
	defer bound.Close()
	// Exercise the real password-to-opaque-credential path over verified TLS.
	// Production ticket delivery/client wrapping is not installed by this test.
	tlsServer := httptest.NewTLSServer(s.Handler())
	defer tlsServer.Close()
	credential := func(name, password string) string {
		t.Helper()
		q := url.Values{"scope": {"storage"}, "credential_type": {"gllive"}, "grant_type": {"password"}, "username": {name}, "password": {password}}
		body := request(t, tlsServer.Client(), "GET", tlsServer.URL+"/authorize?"+q.Encode(), "", 200)
		var response map[string]string
		if err := json.Unmarshal([]byte(body), &response); err != nil {
			t.Fatal(err)
		}
		return response["access_token"]
	}
	tokens := []string{credential("alice", "native-test-password"), credential("bob", "bob-test-password")}
	for _, fake := range []string{"alice", alice, a.id, "invalid"} {
		if _, err := bound.Issue(fake, pc.SnapshotV1, time.Now()); !errors.Is(err, match.ErrAuthorization) {
			t.Fatal("identity substituted for credential", err)
		}
	}
	outsider, _ := accounts.Create(s.Store, "eve")
	outsideToken, _ := accounts.Issue(s.Store, outsider.ID, time.Hour)
	if _, err := bound.Issue(outsideToken, pc.SnapshotV1, time.Now()); !errors.Is(err, match.ErrAuthorization) {
		t.Fatal("outsider admitted", err)
	}
	var channels []*secure.Channel
	for _, token := range tokens {
		for _, unsupported := range []uint32{0, 2, 3} {
			if _, err := bound.Issue(token, unsupported, time.Now()); !errors.Is(err, match.ErrAuthorization) {
				t.Fatal("unsupported capability issued a ticket", err)
			}
		}
		ticket, err := bound.Issue(token, pc.SnapshotV1, time.Now())
		if err != nil {
			t.Fatal(err)
		}
		c, err := secure.New(ticket, false)
		if err != nil {
			t.Fatal(err)
		}
		channels = append(channels, c)
		if _, err = bound.Issue(token, pc.SnapshotV1, time.Now()); !errors.Is(err, match.ErrAuthorization) {
			t.Fatal("duplicate ticket", err)
		}
	}
	listen := func() *net.UDPConn {
		t.Helper()
		c, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { c.Close() })
		return c
	}
	host := listen()
	peers := []*net.UDPConn{listen(), listen()}
	hostAddr := host.LocalAddr().(*net.UDPAddr).AddrPort()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- match.RunAuthenticated(ctx, host, bound) }()
	send := func(i int, h game.Header, body []byte) {
		t.Helper()
		raw, err := game.Encode(h, body)
		if err != nil {
			t.Fatal(err)
		}
		envelope, err := pc.Encode(pc.Native, raw)
		if err != nil {
			t.Fatal(err)
		}
		sealed, err := channels[i].Seal(envelope)
		if err != nil {
			t.Fatal(err)
		}
		if _, err = peers[i].WriteToUDPAddrPort(sealed, hostAddr); err != nil {
			t.Fatal(err)
		}
	}
	read := func(i int, kind byte) game.Packet {
		t.Helper()
		peers[i].SetReadDeadline(time.Now().Add(3 * time.Second))
		for {
			buf := make([]byte, 4096)
			n, _, err := peers[i].ReadFromUDPAddrPort(buf)
			if err != nil {
				t.Fatal(err)
			}
			raw, err := channels[i].Open(buf[:n])
			if err != nil {
				t.Fatal(err)
			}
			kindPC, payload, err := pc.Decode(raw)
			if err != nil || kindPC != pc.Native {
				t.Fatal("invalid pre-game envelope", err)
			}
			p, err := game.Decode(payload)
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
	// Plain native traffic, even from the correct socket, cannot bypass crypto.
	raw, _ := game.Encode(game.Header{}, game.Discovery())
	peers[0].WriteToUDPAddrPort(raw, hostAddr)
	// Encryption alone cannot negotiate an obsolete application protocol.
	obsolete, _ := channels[0].Seal(raw)
	if _, err := bound.Handle(peers[0].LocalAddr().(*net.UDPAddr).AddrPort(), obsolete, time.Now()); err == nil {
		t.Fatal("accepted envelope downgrade")
	}
	for i, name := range []string{"alice", "bob"} {
		send(i, game.Header{}, game.Discovery())
		read(i, game.ServerDetails)
		body, _ := (game.Player{Name: name}).Message()
		send(i, game.Header{Reliable: true}, body)
		read(i, game.Roster)
	}
	read(0, game.Roster)
	if bound.Snapshot().Joined != 2 {
		t.Fatal("lost account-to-slot binding")
	}
	if err = bound.RequestLoading(); err != nil {
		t.Fatal(err)
	}
	send(0, game.Header{Ack: 1, AckMask: 3}, []byte{game.Acknowledge})
	send(1, game.Header{AckMask: 1}, []byte{game.Acknowledge})
	for i := range peers {
		p := read(i, game.StartLoading)
		if len(p.Messages) != 2 || p.Messages[0][0] != game.Roster || p.Messages[1][0] != game.StartLoading || p.Messages[1][1] != byte(i) {
			t.Fatal("cross-account player slot")
		}
		send(i, game.Header{Reliable: true, Sequence: 1, Ack: p.Header.Sequence, AckMask: 1}, []byte{game.FinishedLoading})
		for p = read(i, game.Acknowledge); p.Header.Ack != 1; p = read(i, game.Acknowledge) {
		}
	}
	// Revocation is checked again during the session, not just ticket issuance.
	if err = accounts.Revoke(s.Store, tokens[1]); err != nil {
		t.Fatal(err)
	}
	select {
	case err = <-done:
		if !errors.Is(err, match.ErrAuthorization) {
			t.Fatal(err)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("revoked credential left match running")
	}
	select {
	case <-reservation.Done():
	default:
		t.Fatal("match did not release lobby")
	}
	if len(s.rooms.registry.List()) != 1 {
		t.Fatal("failed allocation left lobby frozen")
	}
}
