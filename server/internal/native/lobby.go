package native

import (
	"context"
	"encoding/binary"
	"errors"
	"io"
	"net"
	"net/netip"
	"sync"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/protocol/lobby"
	"nova3.test/backend/internal/rooms"
	"nova3.test/backend/internal/sessions"
)

func (s *Services) ServeLobby(ctx context.Context, listener net.Listener) error {
	return s.serveLobby(ctx, listener, s.lobbyConnection)
}

func (s *Services) serveLobby(ctx context.Context, listener net.Listener, handle func(net.Conn)) error {
	stop := context.AfterFunc(ctx, func() { listener.Close() })
	defer stop()
	var mu sync.Mutex
	var handlers sync.WaitGroup
	connections := make(map[net.Conn]struct{})
	defer func() {
		listener.Close()
		mu.Lock()
		for connection := range connections {
			connection.Close()
		}
		mu.Unlock()
		handlers.Wait()
	}()
	for {
		connection, err := listener.Accept()
		if err != nil {
			return err
		}
		if ctx.Err() != nil {
			connection.Close()
			return ctx.Err()
		}
		mu.Lock()
		connections[connection] = struct{}{}
		mu.Unlock()
		handlers.Add(1)
		go func() {
			defer handlers.Done()
			defer func() { mu.Lock(); delete(connections, connection); mu.Unlock() }()
			handle(connection)
		}()
	}
}

func (s *Services) lobbyConnection(connection net.Conn) {
	s.lobbyConnectionAs(connection, "", "")
}

func (s *Services) lobbyConnectionAs(connection net.Conn, principal, credential string) {
	s.lobbyConnectionWithCapabilities(connection, principal, credential, 0)
}

func (s *Services) lobbyConnectionWithCapabilities(connection net.Conn, principal, credential string, capabilities uint32) {
	defer connection.Close()
	ctx, cancel := context.WithCancel(context.Background())
	outgoing := make(chan lobby.Frame, 64)
	writerDone := make(chan struct{})
	go func() {
		defer close(writerDone)
		defer connection.Close()
		for {
			select {
			case <-ctx.Done():
				return
			case frame := <-outgoing:
				if err := connection.SetWriteDeadline(time.Now().Add(3 * time.Second)); err != nil {
					return
				}
				if err := lobby.Write(connection, frame); err != nil {
					return
				}
			}
		}
	}()
	defer func() { cancel(); connection.Close(); <-writerDone }()
	send := func(frame lobby.Frame) bool {
		if (frame.Opcode == lobby.CreateRoomResponse || frame.Opcode == lobby.LaunchResponse || frame.Opcode == lobby.JoinRoomResponse || frame.Opcode == lobby.RoomInfoResponse) && len(frame.Payload) >= 4 {
			s.Log.Printf("native room response opcode=%d status=%#x", frame.Opcode, binary.BigEndian.Uint32(frame.Payload))
			if len(frame.Payload) >= 8 && frame.Opcode != lobby.RoomInfoResponse {
				s.Log.Printf("native room response opcode=%d id_or_count=%d", frame.Opcode, binary.BigEndian.Uint32(frame.Payload[4:]))
			}
		}
		select {
		case <-ctx.Done():
			return false
		case outgoing <- frame:
			return true
		default:
			connection.Close()
			return false
		}
	}
	var peer *roomPeer
	established := false
	var lease *sessions.Lease
	defer func() {
		s.rooms.unregister(peer)
		s.Sessions.Release(lease)
	}()
	for {
		deadline := time.Now().Add(2 * time.Minute)
		if lease != nil {
			if !s.Sessions.Active(lease, time.Now()) {
				return
			}
			if lease.Expires().Before(deadline) {
				deadline = lease.Expires()
			}
		}
		if err := connection.SetReadDeadline(deadline); err != nil {
			return
		}
		frame, err := lobby.Read(connection)
		if err != nil {
			if !errors.Is(err, io.EOF) {
				s.Log.Printf("native lobby connection ended: %v", err)
			}
			return
		}
		if credential != "" {
			account, err := accounts.Resolve(s.Store, credential)
			if err != nil || account != principal {
				return
			}
		}
		if lease != nil && !s.Sessions.Active(lease, time.Now()) {
			return
		}
		if !lobbyPoll(frame.Opcode) {
			s.Log.Printf("native lobby opcode=%d bytes=%d", frame.Opcode, len(frame.Payload))
		}
		var response lobby.Frame
		switch frame.Opcode {
		case lobby.EstablishRequest:
			if len(frame.Payload) != 4 || established {
				return
			}
			established = true
			response = lobby.Frame{Opcode: lobby.Established}
		case lobby.LoginRequest:
			if !established || lease != nil {
				return
			}
			login, parseErr := lobby.ParseLogin(frame.Payload)
			if parseErr == nil && principal != "" && login.UID != principal {
				login.UID, parseErr = accounts.Resolve(s.Store, login.UID)
			}
			if parseErr != nil || (principal != "" && login.UID != principal) || !s.legacyAccount(login.UID) {
				return
			}
			lease, err = s.Sessions.Acquire(login.UID, connection, time.Now())
			if err != nil {
				return
			}
			id, name, identityErr := accounts.LobbyIdentity(s.Store, login.UID)
			if identityErr != nil {
				return
			}
			response, err = lobby.LoginAccepted(id, name)
			if err != nil {
				return
			}
			local, _ := netip.ParseAddrPort(connection.LocalAddr().String())
			peer = &roomPeer{player: rooms.Player{Account: login.UID, ID: id, Name: name}, send: send, credential: credential, local: local,
				social: capabilities&socialCapability != 0, socialEvents: capabilities&socialEventCapability != 0}
			s.Log.Printf("native lobby admitted invitations=%t", peer.social)
			s.rooms.register(peer)
		case lobby.KeepAliveRequest:
			if lease == nil || len(frame.Payload) != 0 {
				return
			}
			response = lobby.Frame{Opcode: lobby.KeepAliveResponse, Payload: make([]byte, 4)}
		case lobby.CreateRoomRequest, lobby.ListRoomsRequest, lobby.JoinRoomRequest, lobby.LeaveRoomRequest,
			lobby.RoomInfoRequest, lobby.GameDataRequest, lobby.PlayerDataRequest, lobby.PlayerStatusRequest, lobby.LaunchRequest,
			lobby.FindRoomRequest, lobby.FindRoomV1Request, lobby.FriendRoomsRequest, lobby.KickRequest, lobby.RejoinRoomRequest:
			// Numeric discovery fields help diagnose mismatched room filters without
			// logging identities, room names, credentials or profile payloads.
			if frame.Opcode == lobby.CreateRoomRequest {
				if v, e := lobby.ParseCreateRoom(frame.Payload); e == nil {
					s.Log.Printf("native room create type=%d flags=%d capacity=%d version=%d parameters=%q", v.Type, v.Flags, v.Capacity, v.Version, v.Parameters)
				}
			}
			if lease == nil || !s.rooms.handle(peer, frame) {
				return
			}
			continue
		default:
			s.Log.Printf("native lobby unsupported opcode=%d", frame.Opcode)
			return
		}
		if !send(response) {
			return
		}
	}
}

func lobbyPoll(opcode uint32) bool {
	return opcode == lobby.KeepAliveRequest || opcode == lobby.ListRoomsRequest ||
		opcode == lobby.FindRoomRequest || opcode == lobby.FindRoomV1Request ||
		opcode == lobby.FriendRoomsRequest || opcode == lobby.PlayerCountRequest
}
