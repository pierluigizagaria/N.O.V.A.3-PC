package native

import (
	"encoding/binary"
	"errors"
	"net/netip"
	"strconv"
	"strings"
	"sync"
	"time"

	"nova3.test/backend/internal/protocol/lobby"
	"nova3.test/backend/internal/rooms"
)

type roomPeer struct {
	social       bool
	socialEvents bool
	player       rooms.Player
	send         func(lobby.Frame) bool
	credential   string
	local        netip.AddrPort
}

// The gate orders mutations and bounded outbound enqueueing together. Network
// writes never run under this lock. Each connection has exactly one writer.
type roomService struct {
	mu          sync.Mutex
	registry    rooms.Registry
	peers       map[string]*roomPeer
	matches     *matchHub
	invitations map[string]map[string]gameInvitation
}

func (s *roomService) register(p *roomPeer) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.peers == nil {
		s.peers = map[string]*roomPeer{}
	}
	// Expiry can admit a fresh lease before the closed socket's handler runs its
	// defer. Retire its room membership before publishing the new connection.
	if old := s.peers[p.player.Account]; old != nil {
		s.leave(old)
	}
	s.peers[p.player.Account] = p
	s.replayInvitations(p, time.Now())
}
func playerRecord(p rooms.Player) lobby.PlayerRecord {
	return lobby.PlayerRecord{ID: p.ID, Name: p.Name, Status: p.Status, Data: p.Data}
}
func roomError(err error) uint32 {
	switch {
	case errors.Is(err, rooms.ErrMissing):
		return 0x4016
	case errors.Is(err, rooms.ErrFull):
		return 0x400e
	case errors.Is(err, rooms.ErrOwner):
		return 0x4023
	default:
		return 0x4001
	}
}
func (s *roomService) leave(p *roomPeer) {
	r, ok := s.registry.Leave(p.player.Account)
	if !ok {
		return
	}
	current, currentErr := s.registry.Get(r.ID)
	// The native callback reads current owner ID, then departing player ID.
	// Destroying an owner's room uses the original removal push for each remaining
	// member, whose own ID makes the callback clear the room and player list.
	for _, member := range r.Players {
		if member.Account != p.player.Account {
			if other := s.peers[member.Account]; other != nil {
				opcode, departing := lobby.MemberLeft, p.player.ID
				owner := r.Owner.ID
				if currentErr == nil {
					owner = current.Owner.ID
				}
				if currentErr != nil && r.Owner.Account == p.player.Account {
					opcode, departing = lobby.MemberRemoved, member.ID
				}
				body := append(lobby.String(owner), lobby.String(departing)...)
				other.send(lobby.Frame{Opcode: opcode, Payload: body})
			}
		}
	}
}
func (s *roomService) unregister(p *roomPeer) {
	if p == nil {
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.peers[p.player.Account] != p {
		return
	}
	s.leave(p)
	delete(s.peers, p.player.Account)
}

// A match transport failure removes only the exact old membership. A new
// connection or a later room created while Tick releases its lock is untouched.
func (s *roomService) retireMatchPeer(roomID uint32, p *roomPeer) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if p == nil || s.peers[p.player.Account] != p {
		return
	}
	r, err := s.registry.Current(p.player.Account)
	if err != nil || r.ID != roomID {
		return
	}
	s.leave(p)
	p.send(lobby.Frame{Opcode: lobby.MemberRemoved, Payload: append(lobby.String(r.Owner.ID), lobby.String(p.player.ID)...)})
}

func (s *roomService) handle(p *roomPeer, f lobby.Frame) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	if p == nil || s.peers[p.player.Account] != p {
		return false
	}
	switch f.Opcode {
	case lobby.RejoinRoomRequest:
		if len(f.Payload) != 4 {
			return p.send(lobby.RoomStatus(lobby.RejoinRoomResponse, 0x4001))
		}
		r, err := s.registry.Current(p.player.Account)
		if err != nil || r.ID != binary.BigEndian.Uint32(f.Payload) {
			return p.send(lobby.RoomStatus(lobby.RejoinRoomResponse, 0x4016))
		}
		// Rejoining a running simulation requires a fresh scene admission and
		// UDP grant. Never terminate it or fabricate that capability here.
		if r.Reserved() {
			return p.send(lobby.RoomStatus(lobby.RejoinRoomResponse, 0x4001))
		}
		players := make([]lobby.PlayerRecord, 0, len(r.Players))
		for _, member := range r.Players {
			players = append(players, playerRecord(member))
		}
		response, err := lobby.RejoinedRoom(roomRecord(r), players)
		if err != nil {
			return p.send(lobby.RoomStatus(lobby.RejoinRoomResponse, 0x4001))
		}
		return p.send(response)
	case lobby.FindRoomRequest, lobby.FindRoomV1Request, lobby.FriendRoomsRequest:
		var names []string
		var params []lobby.Parameter
		opcode, version := lobby.FindRoomResponse, 2
		var err error
		if f.Opcode == lobby.FriendRoomsRequest {
			opcode = lobby.FriendRoomsResponse
			var v lobby.FriendRooms
			v, err = lobby.ParseFriendRooms(f.Payload)
			if err == nil && (v.Type != 1 || v.Flags != 1 || !validProperties(v.Parameters, 2)) {
				err = rooms.ErrCapacity
			}
			names, params = v.Names, v.Parameters
		} else {
			if f.Opcode == lobby.FindRoomV1Request {
				opcode, version = lobby.FindRoomV1Response, 1
			}
			var name string
			name, err = lobby.ParseFindRoom(f.Payload)
			names = []string{name}
		}
		if err != nil {
			return p.send(lobby.RoomStatus(opcode, 0x4001))
		}
		records := []lobby.RoomRecord{}
		for _, room := range s.listedRooms() {
			if room.Settings.Type != 1 || !matches(room, params) {
				continue
			}
			found := false
			for _, name := range names {
				for _, member := range room.Players {
					if strings.EqualFold(member.Name, name) || member.ID == name {
						found = true
					}
				}
			}
			if !found {
				continue
			}
			candidate := append(records, roomRecord(room))
			if _, e := lobby.RoomListVersion(candidate, opcode, version); e != nil {
				break
			}
			records = candidate
			if len(records) == 30 {
				break
			}
		}
		response, err := lobby.RoomListVersion(records, opcode, version)
		if err != nil {
			return p.send(lobby.RoomStatus(opcode, 0x4001))
		}
		return p.send(response)
	case lobby.KickRequest:
		id, err := lobby.ParsePlayerData(f.Payload)
		if err != nil || len(id) == 0 || len(id) > 16 {
			return p.send(lobby.RoomStatus(lobby.KickResponse, 0x4001))
		}
		room, removed, err := s.registry.Kick(p.player.Account, id)
		if err != nil {
			return p.send(lobby.RoomStatus(lobby.KickResponse, roomError(err)))
		}
		// The owner's success callback removes its selected row. Other members
		// (including the removed one) consume the standard removal push.
		ok := p.send(lobby.RoomStatus(lobby.KickResponse, 0))
		body := append(lobby.String(room.Owner.ID), lobby.String(removed.ID)...)
		for _, member := range room.Players {
			if member.Account != p.player.Account {
				if other := s.peers[member.Account]; other != nil {
					other.send(lobby.Frame{Opcode: lobby.MemberRemoved, Payload: body})
				}
			}
		}
		return ok
	case lobby.CreateRoomRequest:
		v, err := lobby.ParseCreateRoom(f.Payload)
		if err != nil || v.Name != p.player.ID || !lobby.ValidGameData(v.GameData) || !validProperties(v.Parameters, 0) {
			return p.send(lobby.RoomStatus(lobby.CreateRoomResponse, 0x4001))
		}
		settings := rooms.Settings{Type: v.Type, Flags: v.Flags, Capacity: int(v.Capacity), Data: v.GameData, Version: v.Version}
		for _, x := range v.Parameters {
			settings.Properties = append(settings.Properties, rooms.Property{ID: x.ID, Operator: x.Operator, Value: x.Value})
		}
		player := p.player
		player.Data = v.PlayerData
		r, err := s.registry.Create(player, settings)
		if err != nil {
			return p.send(lobby.RoomStatus(lobby.CreateRoomResponse, roomError(err)))
		}
		s.bindInvitations(p.player.Account, r.ID, time.Now())
		return p.send(lobby.Frame{Opcode: lobby.CreateRoomResponse, Payload: lobby.Integers(0, r.ID)})
	case lobby.ListRoomsRequest:
		v, err := lobby.ParseListRooms(f.Payload)
		if err != nil || v.Limit == 0 || v.Type != 1 || v.Flags != 1 || !validProperties(v.Parameters, 2) {
			return p.send(lobby.RoomStatus(lobby.ListRoomsResponse, 0x4001))
		}
		all := s.listedRooms()
		records := []lobby.RoomRecord{}
		var skipped uint32
		for _, r := range all {
			if r.Settings.Type != v.Type || !matches(r, v.Parameters) {
				continue
			}
			if skipped < v.Offset {
				skipped++
				continue
			}
			candidate := append(records, roomRecord(r))
			if _, e := lobby.RoomList(candidate); e != nil {
				break
			}
			records = candidate
			if len(records) >= int(v.Limit) {
				break
			}
		}
		response, err := lobby.RoomList(records)
		if err != nil {
			return p.send(lobby.RoomStatus(lobby.ListRoomsResponse, 0x4001))
		}
		return p.send(response)
	case lobby.JoinRoomRequest:
		v, err := lobby.ParseJoinRoom(f.Payload)
		if err != nil || v.Status > 3 {
			return p.send(lobby.RoomStatus(lobby.JoinRoomResponse, 0x4001))
		}
		player := p.player
		player.Data = v.PlayerData
		player.Status = v.Status
		r, err := s.registry.Join(player, v.ID)
		if err != nil {
			return p.send(lobby.RoomStatus(lobby.JoinRoomResponse, roomError(err)))
		}
		var handoff []lobby.Frame
		if r.Reserved() {
			handoff, err = s.matches.admit(r, p)
			if err != nil {
				s.leave(p)
				return p.send(lobby.RoomStatus(lobby.JoinRoomResponse, roomError(err)))
			}
		}
		players := make([]lobby.PlayerRecord, 0, len(r.Players))
		for _, m := range r.Players {
			players = append(players, playerRecord(m))
		}
		response, err := lobby.JoinedRoom(players)
		if err != nil {
			s.leave(p)
			return p.send(lobby.RoomStatus(lobby.JoinRoomResponse, 0x4001))
		}
		if !p.send(response) {
			s.leave(p)
			return false
		}
		body, err := lobby.PlayerBytes(playerRecord(player))
		if err != nil {
			return false
		}
		for _, m := range r.Players {
			if m.Account != player.Account {
				if other := s.peers[m.Account]; other != nil {
					other.send(lobby.Frame{Opcode: lobby.MemberJoined, Payload: body})
				}
			}
		}
		for _, frame := range handoff {
			if !p.send(frame) {
				s.leave(p)
				return false
			}
		}
		s.bindInvitations(p.player.Account, r.ID, time.Now())
		return true
	case lobby.RoomInfoRequest:
		id, withPlayers, err := lobby.ParseRoomInfo(f.Payload)
		if err != nil || withPlayers > 1 {
			return p.send(lobby.RoomStatus(lobby.RoomInfoResponse, 0x4001))
		}
		r, err := s.registry.Get(id)
		if err != nil {
			return p.send(lobby.RoomStatus(lobby.RoomInfoResponse, roomError(err)))
		}
		var players []lobby.PlayerRecord
		if withPlayers == 1 {
			for _, member := range r.Players {
				players = append(players, playerRecord(member))
			}
		}
		response, err := lobby.RoomInfo(roomRecord(r), players...)
		if err != nil {
			return p.send(lobby.RoomStatus(lobby.RoomInfoResponse, 0x4001))
		}
		return p.send(response)
	case lobby.GameDataRequest:
		data, params, err := lobby.ParseGameData(f.Payload)
		if err != nil || !lobby.ValidGameData(data) || !validProperties(params, 0) {
			return p.send(lobby.RoomStatus(lobby.GameDataResponse, 0x4001))
		}
		properties := []rooms.Property{}
		for _, v := range params {
			properties = append(properties, rooms.Property{ID: v.ID, Operator: v.Operator, Value: v.Value})
		}
		r, err := s.registry.UpdateSettings(p.player.Account, data, properties)
		if err != nil {
			return p.send(lobby.RoomStatus(lobby.GameDataResponse, roomError(err)))
		}
		s.broadcast(r, p, lobby.Frame{Opcode: lobby.GameDataChanged, Payload: lobby.String(data)})
		return p.send(lobby.RoomStatus(lobby.GameDataResponse, 0))
	case lobby.PlayerDataRequest, lobby.PlayerStatusRequest:
		var data *string
		var status *byte
		response := lobby.PlayerDataResponse
		if f.Opcode == lobby.PlayerDataRequest {
			v, err := lobby.ParsePlayerData(f.Payload)
			if err != nil {
				return p.send(lobby.RoomStatus(response, 0x4001))
			}
			data = &v
		} else {
			response = lobby.PlayerStatusResponse
			// CLobbySkin::IsHandleEventSetReady sends 3. Its success callback
			// changes the local player from 2 to 3; rejecting it retries forever.
			if len(f.Payload) != 1 || f.Payload[0] > 3 {
				return p.send(lobby.RoomStatus(response, 0x4001))
			}
			status = &f.Payload[0]
		}
		r, player, err := s.registry.UpdatePlayer(p.player.Account, data, status)
		if err != nil {
			return p.send(lobby.RoomStatus(response, roomError(err)))
		}
		body := lobby.String(player.ID)
		opcode := lobby.PlayerStatusChanged
		if data != nil {
			opcode = lobby.PlayerDataChanged
			body = append(body, lobby.String(player.Name)...)
			body = append(body, lobby.String(player.Data)...)
		} else {
			body = append(body, player.Status)
		}
		s.broadcast(r, p, lobby.Frame{Opcode: opcode, Payload: body})
		return p.send(lobby.RoomStatus(response, 0))
	case lobby.LaunchRequest:
		if len(f.Payload) != 0 {
			return p.send(lobby.RoomStatus(lobby.LaunchResponse, 0x4001))
		}
		r, err := s.registry.Current(p.player.Account)
		if err != nil {
			return p.send(lobby.RoomStatus(lobby.LaunchResponse, 0x4022))
		}
		if r.Owner.Account != p.player.Account {
			return p.send(lobby.RoomStatus(lobby.LaunchResponse, 0x4023))
		}
		return s.launch(p, r)
	case lobby.LeaveRoomRequest:
		if len(f.Payload) != 0 {
			return p.send(lobby.RoomStatus(lobby.LeaveRoomResponse, 0x4001))
		}
		s.leave(p)
		return p.send(lobby.RoomStatus(lobby.LeaveRoomResponse, 0))
	}
	return false
}

func roomRecord(r rooms.Room) lobby.RoomRecord {
	// Both UpdateHostList and QuickPlay read the BE32 field at native room
	// +0x14. The earlier byte is a separate field and cannot replace it.
	var started byte
	if r.Reserved() {
		started = 1
	}
	return lobby.RoomRecord{ID: r.ID, Name: r.Owner.ID, Owner: r.Owner.Name, Capacity: byte(r.Settings.Capacity), Started: started, Type: r.Settings.Type, Flags: r.Settings.Flags, PlayerCount: uint32(len(r.Players)), GameData: r.Settings.Data, Version: r.Settings.Version}
}
func (s *roomService) broadcast(r rooms.Room, except *roomPeer, f lobby.Frame) {
	for _, member := range r.Players {
		if other := s.peers[member.Account]; other != nil && other != except {
			other.send(f)
		}
	}
}

// oconf.bar defines three integer properties: tournament, mode and map.
// Menu_ApplyFilters uses comparison 2 for all three exact selections. Other
// operators are rejected until their native semantics are established.
func validProperties(params []lobby.Parameter, operator byte) bool {
	for _, p := range params {
		if p.ID > 2 || p.Operator != operator {
			return false
		}
		if _, err := strconv.ParseInt(p.Value, 10, 32); err != nil {
			return false
		}
	}
	return true
}
func matches(r rooms.Room, params []lobby.Parameter) bool {
	for _, p := range params {
		found := false
		want, _ := strconv.ParseInt(p.Value, 10, 32)
		for _, v := range r.Settings.Properties {
			got, _ := strconv.ParseInt(v.Value, 10, 32)
			if v.ID == p.ID && got == want {
				found = true
				break
			}
		}
		if !found {
			return false
		}
	}
	return true
}
