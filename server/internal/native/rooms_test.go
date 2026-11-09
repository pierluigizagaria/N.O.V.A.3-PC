package native

import (
	"bytes"
	"encoding/binary"
	"net"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/grants"
	"nova3.test/backend/internal/protocol/lobby"
)

type roomClient struct {
	conn net.Conn
	done chan struct{}
	id   string
}

func connectRoomClient(t *testing.T, s *Services, account string) *roomClient {
	t.Helper()
	if err := s.Sessions.Login(account, time.Now(), time.Hour); err != nil {
		t.Fatal(err)
	}
	c, server := net.Pipe()
	v := &roomClient{conn: c, done: make(chan struct{})}
	go func() { s.lobbyConnection(server); close(v.done) }()
	t.Cleanup(func() { v.close(t) })
	v.exchange(t, lobby.EstablishRequest, make([]byte, 4), lobby.Established)
	b := append(make([]byte, 4), lobby.String(account)...)
	b = append(b, 0, 0, 0, 0, 1)
	reply := v.exchange(t, lobby.LoginRequest, b, lobby.LoginResponse)
	if len(reply) < 6 {
		t.Fatal("login reply")
	}
	n := int(binary.BigEndian.Uint16(reply[4:6]))
	if n == 0 || n > 16 || len(reply) < 6+n {
		t.Fatal("identity bounds")
	}
	v.id = string(reply[6 : 6+n])
	return v
}
func (v *roomClient) close(t *testing.T) {
	t.Helper()
	v.conn.Close()
	select {
	case <-v.done:
	case <-time.After(4 * time.Second):
		t.Error("lobby handler/writer leaked")
	}
}
func (v *roomClient) read(t *testing.T, opcode uint32) []byte {
	t.Helper()
	v.conn.SetDeadline(time.Now().Add(4 * time.Second))
	f, e := lobby.Read(v.conn)
	if e != nil || f.Opcode != opcode {
		t.Fatalf("read %d: %+v %v", opcode, f, e)
	}
	return f.Payload
}
func (v *roomClient) exchange(t *testing.T, opcode uint32, payload []byte, response uint32) []byte {
	t.Helper()
	v.conn.SetDeadline(time.Now().Add(4 * time.Second))
	if e := lobby.Write(v.conn, lobby.Frame{Opcode: opcode, Payload: payload}); e != nil {
		t.Fatal(e)
	}
	return v.read(t, response)
}
func createRoomPayload(id string) []byte {
	b := append(lobby.String(id), 1, 0, 0, 2)
	b = append(b, lobby.String("0|1|2|3|4|5|6|7|8|9|10|room")...)
	b = append(b, lobby.String("alice-data")...)
	b = append(b, 1, 0, 0, 0, 1, '0')
	return append(b, lobby.Integers(107)...)
}
func queryRooms(value byte) []byte {
	b := append(lobby.Integers(1), 50)
	b = append(b, lobby.Integers(0)...)
	return append(b, 1, 0, 2, 0, 1, value, 1, 1)
}
func status(t *testing.T, b []byte, want uint32) {
	t.Helper()
	if len(b) != 4 || binary.BigEndian.Uint32(b) != want {
		t.Fatalf("status %x want %x", b, want)
	}
}

func checkListedPlayers(t *testing.T, payload []byte, want uint32) {
	t.Helper()
	if len(payload) < 12 || binary.BigEndian.Uint32(payload[4:8]) != 1 {
		t.Fatal("expected one listed room", payload)
	}
	offset := 12
	for range 2 { // Room ID and display name are counted strings.
		if offset+2 > len(payload) {
			t.Fatal("truncated room identity")
		}
		offset += 2 + int(binary.BigEndian.Uint16(payload[offset:]))
	}
	if offset+12 > len(payload) || payload[offset+1] != 0 || binary.BigEndian.Uint32(payload[offset+4:]) != want {
		t.Fatal("waiting room must expose an unstarted flag and real BE32 player count", payload, want)
	}
}

func TestTwoLobbyClientsRoomsUpdatesDisconnectAndReentry(t *testing.T) {
	s, alice, _, _ := fixture(t)
	bob, e := accounts.Create(s.Store, "bob")
	if e != nil {
		t.Fatal(e)
	}
	if e = grants.Provision(s.Store, bob.ID, s.Shop.Catalogue.Version); e != nil {
		t.Fatal(e)
	}
	a := connectRoomClient(t, s, alice)
	b := connectRoomClient(t, s, bob.ID)
	if a.id == b.id {
		t.Fatal("shared native identity")
	}
	status(t, a.exchange(t, 135, createRoomPayload(b.id), 12), 0x4001)
	created := a.exchange(t, 135, createRoomPayload(a.id), 12)
	if len(created) != 8 || binary.BigEndian.Uint32(created) != 0 {
		t.Fatal(created)
	}
	id := binary.BigEndian.Uint32(created[4:])
	status(t, b.exchange(t, lobby.RejoinRoomRequest, lobby.Integers(id), lobby.RejoinRoomResponse), 0x4016)
	// Original SetReady sends 3 (not 1); error responses leave its local state
	// unchanged and cause another request on every lobby update.
	status(t, a.exchange(t, 39, []byte{3}, 40), 0)
	ready, err := s.rooms.registry.Current(alice)
	if err != nil || ready.Owner.Status != 3 {
		t.Fatal("owner ready state", ready, err)
	}
	status(t, a.exchange(t, 39, []byte{4}, 40), 0x4001)
	status(t, a.exchange(t, 135, createRoomPayload(a.id), 12), 0x4001)
	empty := b.exchange(t, 128, queryRooms('1'), 129)
	if !bytes.Equal(empty, lobby.Integers(0, 0)) {
		t.Fatal("filter ignored", empty)
	}
	list := b.exchange(t, 128, queryRooms('0'), 129)
	checkListedPlayers(t, list, 1)
	if len(list) < 12 || binary.BigEndian.Uint32(list[4:]) != 1 || binary.BigEndian.Uint32(list[8:]) != id {
		t.Fatal("room not discoverable", list)
	}
	for _, pair := range [][2]uint32{{130, 131}, {97, 98}} {
		found := b.exchange(t, pair[0], append(make([]byte, 9), lobby.String(a.id)...), pair[1])
		if len(found) < 12 || binary.BigEndian.Uint32(found[4:]) != 1 || binary.BigEndian.Uint32(found[8:]) != id {
			t.Fatal("name search", found)
		}
	}
	friendQuery := append(lobby.Integers(23), 1, 0, 0, 0, 0, 1, 1)
	friendQuery = append(friendQuery, lobby.String(a.id)...)
	friendQuery = append(friendQuery, 0, 1)
	if found := b.exchange(t, 132, friendQuery, 133); !bytes.Equal(found, list) {
		t.Fatal("friend room search", found)
	}
	status(t, b.exchange(t, 15, append(append(lobby.Integers(id+1), 0), lobby.String("bob-data")...), 16), 0x4016)
	joined := b.exchange(t, 15, append(append(lobby.Integers(id), 3), lobby.String("bob-data")...), 16)
	if len(joined) < 8 || binary.BigEndian.Uint32(joined[4:]) != 2 {
		t.Fatal("membership", joined)
	}
	a.read(t, lobby.MemberJoined)
	checkListedPlayers(t, b.exchange(t, 128, queryRooms('0'), 129), 2)
	rejoined := b.exchange(t, lobby.RejoinRoomRequest, lobby.Integers(id), lobby.RejoinRoomResponse)
	if binary.BigEndian.Uint32(rejoined) != 0 || !bytes.HasSuffix(rejoined, joined[4:]) {
		t.Fatal("rejoin must restore the same roster", rejoined)
	}
	lease, err := s.rooms.registry.Reserve(alice)
	if err != nil {
		t.Fatal(err)
	}
	status(t, b.exchange(t, lobby.RejoinRoomRequest, lobby.Integers(id), lobby.RejoinRoomResponse), 0x4001)
	if _, ok := lease.Member(bob.ID); !ok {
		t.Fatal("rejoin must not invalidate a running match")
	}
	lease.Release()
	joinedRoom, err := s.rooms.registry.Current(bob.ID)
	if err != nil || joinedRoom.Players[1].Status != 3 {
		t.Fatal("joiner requested ready status must be retained", joinedRoom, err)
	}
	status(t, b.exchange(t, lobby.KickRequest, lobby.String(a.id), lobby.KickResponse), 0x4023)
	info := b.exchange(t, lobby.RoomInfoRequest, append(lobby.Integers(id), 1), lobby.RoomInfoResponse)
	if !bytes.HasSuffix(info, joined[4:]) {
		t.Fatal("extended room info must include the native roster", info)
	}
	status(t, b.exchange(t, 39, []byte{3}, 40), 0)
	if got := a.read(t, 41); !bytes.Equal(got, append(lobby.String(b.id), 3)) {
		t.Fatal("ready push", got)
	}
	status(t, b.exchange(t, 42, nil, 43), 0x4023)
	status(t, a.exchange(t, 42, nil, 43), 0x4080)
	// A failed launch keeps the lobby alive and the original room membership.
	status(t, b.exchange(t, 39, []byte{1}, 40), 0)
	if got := a.read(t, 41); !bytes.Equal(got, append(lobby.String(b.id), 1)) {
		t.Fatal("status push", got)
	}
	status(t, b.exchange(t, 36, lobby.String("changed-bob"), 37), 0)
	a.read(t, 38)
	r, e := s.rooms.registry.Current(bob.ID)
	if e != nil || r.Players[0].Data != "alice-data" || r.Players[1].Data != "changed-bob" {
		t.Fatal("player contamination", r, e)
	}
	status(t, a.exchange(t, lobby.KickRequest, lobby.String(b.id), lobby.KickResponse), 0)
	if got := b.read(t, lobby.MemberRemoved); !bytes.Equal(got, append(lobby.String(a.id), lobby.String(b.id)...)) {
		t.Fatal("kick push", got)
	}
	if _, err := s.rooms.registry.Current(bob.ID); err == nil {
		t.Fatal("kicked membership retained")
	}
	checkListedPlayers(t, b.exchange(t, 128, queryRooms('0'), 129), 1)
	status(t, a.exchange(t, lobby.KickRequest, lobby.String(b.id), lobby.KickResponse), 0x4016)
	joined = b.exchange(t, 15, append(append(lobby.Integers(id), 3), lobby.String("bob-data")...), 16)
	if len(joined) < 8 || binary.BigEndian.Uint32(joined[4:]) != 2 {
		t.Fatal("rejoin after kick", joined)
	}
	a.read(t, lobby.MemberJoined)
	a.close(t)
	if got := b.read(t, 32); !bytes.Equal(got, append(lobby.String(a.id), lobby.String(b.id)...)) {
		t.Fatal("owner destruction push", got)
	}
	if len(s.rooms.registry.List()) != 0 {
		t.Fatal("orphan room")
	}
	created = b.exchange(t, 135, createRoomPayload(b.id), 12)
	if len(created) != 8 || binary.BigEndian.Uint32(created) != 0 {
		t.Fatal("reentry", created)
	}
	b.close(t)
	if len(s.rooms.registry.List()) != 0 || s.Sessions.Count(time.Now()) != 0 {
		t.Fatal("disconnect leaked membership or presence")
	}
}
