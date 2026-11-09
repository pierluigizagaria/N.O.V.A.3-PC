package application

import (
	"context"
	"crypto/tls"
	"encoding/binary"
	"net"
	"net/netip"
	"os"
	"path/filepath"
	"testing"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/protocol/lobby"
	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/protocol/secure"
)

// Full production composition: password/TLS lobby -> Launch -> ticket/address
// -> encrypted discovery/rosters/loading/clock/start -> two-way precise poses.
// This is a protocol check, not an observed game/UI acceptance run.
func TestOnlineLobbyToMovementAndTeardown(t *testing.T) {
	for _, mode := range []string{"loopback", "address", "proxy"} {
		t.Run(mode, func(t *testing.T) { onlineMovement(t, mode, false) })
	}
}

func TestOnlineLateJoinToMovementAndTeardown(t *testing.T) {
	for _, mode := range []string{"loopback", "address", "proxy"} {
		t.Run(mode, func(t *testing.T) { onlineMovement(t, mode, true) })
	}
}

func onlineMovement(t *testing.T, mode string, late bool) {
	db, adapter, options, ids := setup(t)
	if mode == "address" {
		options.Address = "127.0.0.1"
	}
	if mode == "proxy" {
		options.ProxyTLS = true
		options.Domain = "game.example.org"
		options.Directory = filepath.Join(t.TempDir(), "no-server-certificates")
		options.ResolveIPv4 = func(context.Context, string) ([]netip.Addr, error) {
			return []netip.Addr{netip.MustParseAddr("127.0.0.1")}, nil
		}
	}
	output := readiness{addresses: make(chan map[string]string, 1)}
	options.Ready = output
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() { done <- RunNative(ctx, db, adapter, options) }()
	defer func() { cancel(); stopped(t, done, false) }()
	addresses := ready(t, output.addresses)
	var config *tls.Config
	if mode == "proxy" {
		if _, err := os.Stat(options.Directory); !os.IsNotExist(err) {
			t.Fatal("proxy startup touched server certificates", err)
		}
		config = proxyFrontends(t, addresses)
	} else {
		config = trustedTransport(t, options.Directory)
	}
	var clients [2]net.Conn
	var nativeIDs [2]string
	for i, name := range []string{"alice", "bob"} {
		token := passwordCredential(t, config, addresses["tls"], name)
		clients[i] = protectedConnect(t, config, addresses["lobby"], token)
		login(t, clients[i], token, true)
		nativeIDs[i], _, _ = accounts.LobbyIdentity(db, ids[i])
	}
	read := func(i int, opcode uint32) []byte {
		t.Helper()
		clients[i].SetReadDeadline(time.Now().Add(15 * time.Second))
		f, err := lobby.Read(clients[i])
		if err != nil || f.Opcode != opcode {
			t.Fatalf("lobby %d want=%d got=%d error=%v", i, opcode, f.Opcode, err)
		}
		return f.Payload
	}
	sendLobby := func(i int, opcode uint32, payload []byte) {
		t.Helper()
		clients[i].SetWriteDeadline(time.Now().Add(15 * time.Second))
		if err := lobby.Write(clients[i], lobby.Frame{Opcode: opcode, Payload: payload}); err != nil {
			t.Fatal(err)
		}
	}
	create := append(lobby.String(nativeIDs[0]), 1, 0, 0, 2)
	create = append(create, lobby.String("0|0|300|600|2|1|0|1|0|1|0|room")...)
	create = append(create, lobby.String("0|0")...)
	create = append(create, 0)
	create = append(create, lobby.Integers(107)...)
	sendLobby(0, lobby.CreateRoomRequest, create)
	created := read(0, lobby.CreateRoomResponse)
	if len(created) < 8 || binary.BigEndian.Uint32(created) != 0 {
		t.Fatal("create failed")
	}
	roomID := binary.BigEndian.Uint32(created[4:])
	sendLobby(0, lobby.PlayerStatusRequest, []byte{3})
	if ready := read(0, lobby.PlayerStatusResponse); len(ready) != 4 || binary.BigEndian.Uint32(ready) != 0 {
		t.Fatal("native owner ready failed", ready)
	}
	// Original browser: numeric login user field, page size, offset, no filters,
	// public room type and extended information. It does not send GGI here.
	query := append(lobby.Integers(1), 50)
	query = append(query, lobby.Integers(0)...)
	query = append(query, 0, 1, 1)
	sendLobby(1, lobby.ListRoomsRequest, query)
	listed := read(1, lobby.ListRoomsResponse)
	if len(listed) < 12 || binary.BigEndian.Uint32(listed) != 0 || binary.BigEndian.Uint32(listed[4:]) != 1 || binary.BigEndian.Uint32(listed[8:]) != roomID {
		t.Fatal("created room missing from native browser", listed)
	}
	join := append(lobby.Integers(roomID), 3)
	join = append(join, lobby.String("0|0")...)
	joinSecond := func() {
		sendLobby(1, lobby.JoinRoomRequest, join)
		status := read(1, lobby.JoinRoomResponse)
		if len(status) < 4 || binary.BigEndian.Uint32(status) != 0 {
			t.Fatal("join rejected", status)
		}
		read(0, lobby.MemberJoined)
	}
	if !late {
		joinSecond()
	}
	sendLobby(0, lobby.LaunchRequest, nil)
	if status := read(0, lobby.LaunchResponse); len(status) != 4 || binary.BigEndian.Uint32(status) != 0 {
		t.Fatal("Launch unavailable", status)
	}
	type client struct {
		conn                                           *net.UDPConn
		channel                                        *secure.Channel
		endpoint                                       netip.AddrPort
		next, ack, mask                                uint32
		joined, loaded, started, proposed, sawPeer     bool
		heartbeat                                      bool
		announced                                      bool
		initialized                                    bool
		roundClock, countdown, roundStarted, respawned bool
		spawn                                          uint32
		generation                                     uint32
		position                                       [3]float32
	}
	var peers [2]client
	connectPeer := func(i int) {
		grant := read(i, lobby.MatchControlPush)
		if len(grant) != 92 || grant[1] != 1 {
			t.Fatal("missing grant")
		}
		var ticket secure.Ticket
		copy(ticket.ID[:], grant[12:28])
		copy(ticket.ClientKey[:], grant[28:60])
		copy(ticket.ServerKey[:], grant[60:92])
		peers[i].channel, _ = secure.New(ticket, false)
		peers[i].endpoint = netip.AddrPortFrom(netip.AddrFrom4([4]byte(grant[2:6])), binary.BigEndian.Uint16(grant[6:8]))
		read(i, lobby.GameEndpointPush)
		conn, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { conn.Close() })
		peers[i].conn = conn
	}
	connectPeer(0)
	if !late {
		connectPeer(1)
	}
	sealed := func(i int, kind byte, body []byte) {
		t.Helper()
		plain, err := pc.Encode(kind, body)
		if err != nil {
			t.Fatal(err)
		}
		data, err := peers[i].channel.Seal(plain)
		if err != nil {
			t.Fatal(err)
		}
		if _, err = peers[i].conn.WriteToUDPAddrPort(data, peers[i].endpoint); err != nil {
			t.Fatal(err)
		}
	}
	send := func(i int, body []byte, reliable bool) {
		t.Helper()
		p := &peers[i]
		data, err := game.Encode(game.Header{Reliable: reliable, Sequence: p.next, Ack: p.ack, AckMask: p.mask}, body)
		if err != nil {
			t.Fatal(err)
		}
		if reliable {
			p.next++
		}
		sealed(i, pc.Native, data)
	}
	for i := range peers {
		if peers[i].conn != nil {
			send(i, game.Discovery(), false)
		}
	}
	epoch := time.Now()
	deadline := epoch.Add(25 * time.Second)
	complete := func(p client) bool {
		return p.sawPeer && p.heartbeat && p.initialized && p.roundClock && p.countdown && p.roundStarted && p.respawned && p.generation == 2
	}
	for !complete(peers[0]) || !complete(peers[1]) {
		if time.Now().After(deadline) {
			t.Fatalf("movement deadline: started=%v/%v proposed=%v/%v seen=%v/%v", peers[0].started, peers[1].started, peers[0].proposed, peers[1].proposed, peers[0].sawPeer, peers[1].sawPeer)
		}
		if late && peers[1].conn == nil && peers[0].initialized {
			sendLobby(1, lobby.ListRoomsRequest, query)
			listed := read(1, lobby.ListRoomsResponse)
			if len(listed) < 12 || binary.BigEndian.Uint32(listed[4:]) != 1 || binary.BigEndian.Uint32(listed[8:]) != roomID {
				t.Fatal("running room missing", listed)
			}
			joinSecond()
			connectPeer(1)
			send(1, game.Discovery(), false)
		}
		for i := range peers {
			p := &peers[i]
			if p.conn == nil {
				continue
			}
			p.conn.SetReadDeadline(time.Now().Add(10 * time.Millisecond))
			var buffer [4096]byte
			n, _, err := p.conn.ReadFromUDPAddrPort(buffer[:])
			if e, ok := err.(net.Error); ok && e.Timeout() {
				continue
			}
			if err != nil {
				t.Fatal(err)
			}
			plain, err := p.channel.Open(buffer[:n])
			if err != nil {
				t.Fatal(err)
			}
			kind, body, err := pc.Decode(plain)
			if err != nil {
				t.Fatal(err)
			}
			switch kind {
			case pc.Native:
				packet, err := game.Decode(body)
				if err != nil {
					t.Fatal(err)
				}
				if packet.Header.Reliable {
					seq := packet.Header.Sequence
					if p.mask == 0 {
						p.ack, p.mask = seq, 1
					} else if seq > p.ack {
						p.mask = (p.mask << (seq - p.ack)) | 1
						p.ack = seq
					} else if p.ack-seq < 32 {
						p.mask |= 1 << (p.ack - seq)
					}
					// Match the original client's idle ACK scheduler: one received
					// reliable packet alone does not trigger an ACK-only datagram.
					if p.mask&(p.mask-1) != 0 {
						send(i, []byte{game.Acknowledge}, false)
					}
				}
				for _, m := range packet.Messages {
					switch m[0] {
					case game.ServerDetails:
						if !p.joined {
							name := []string{"alice", "bob"}[i]
							details, _ := (game.Player{Name: name, Beacon: [16]byte{6: 1, 9: 1}}).Message()
							send(i, details, true)
							p.joined = true
						}
					case game.StartLoading:
						if m[1] != byte(i) || m[2] != 0 || m[11] != 0 {
							t.Fatal("loading fields", m)
						}
						if !p.loaded {
							loadout := make([]byte, 12)
							loadout[0], loadout[1] = game.PlayerLoadout, byte(i)
							loadout[4], loadout[7] = 2, 1
							send(i, loadout, true)
							send(i, []byte{game.FinishedLoading}, true)
							p.loaded = true
						}
					case game.RequestClock:
						time.Sleep(time.Millisecond)
						response, _ := game.ClockSample(m[1], uint32(time.Since(epoch).Milliseconds())+1000)
						send(i, response, false)
					case game.StartGame:
						if !p.started {
							send(i, []byte{game.SceneReady}, true)
						}
						p.started = true
						p.spawn = binary.BigEndian.Uint32(m[13:])
					case game.NewPlayer:
						presence, err := game.ReadPresence(m)
						if err != nil || int(presence.Player.Slot) != 1-i || presence.Player.Name != []string{"alice", "bob"}[1-i] {
							t.Fatal("remote scene announcement", presence, err)
						}
						p.announced = true
					case game.PlayersSnapshot:
						row := m[25+i*63 : 25+(i+1)*63]
						if len(m) != 781 || row[0] != 1 || row[1] != 1 || row[3] != byte(i) || row[10] != 2 || binary.BigEndian.Uint16(row[57:]) != 101 {
							t.Fatal("missing original scene initialization or catalogue weapon")
						}
						p.initialized = true
						other := m[25+(1-i)*63:]
						if other[0] == 1 && other[1] == 1 && other[3] == byte(1-i) {
							p.announced = true
						}
						send(i, []byte{game.RequestPowerups}, true)
					case game.Ping:
						if p.started {
							p.heartbeat = true
						}
						// Original client HandlePing echoes the sequence, with RTT=0.
						reply := append([]byte(nil), m...)
						clear(reply[5:])
						send(i, reply, false)
					case game.RoundSnapshot:
						if _, err := game.ReadRoundClock(m); err != nil {
							t.Fatal(err)
						}
						p.roundClock = true
					case game.RoundCountdown:
						p.countdown = true
					case game.RoundStarted:
						if !p.roundStarted {
							send(i, []byte{game.RequestRespawn, byte(i)}, true)
						}
						p.roundStarted = true
					case game.RespawnPlayer:
						r, err := game.ReadRespawn(m)
						if err != nil {
							t.Fatal(err)
						}
						if r.Slot == byte(i) {
							if r.Spawn != p.spawn {
								t.Fatal("round spawn was not reserved", r)
							}
							p.respawned = true
						}
					}
				}
			case pc.Lives:
				lives, _ := pc.DecodeLives(body)
				if len(lives) < 1 || len(lives) > 2 {
					t.Fatal(lives)
				}
				if len(lives) <= i {
					continue
				}
				if p.generation != lives[i].Generation {
					p.proposed, p.sawPeer = false, false
					p.generation = lives[i].Generation
				}
			case pc.Snapshots:
				poses, _ := pc.DecodeSnapshots(body)
				if len(poses) > i && p.started && p.generation != 0 && poses[i].Generation == p.generation && !p.proposed {
					v := poses[i]
					v.Sequence = 1
					v.Position[0] += float32(i+1) * 1.25
					v.Yaw = 12345
					v.Pitch = 54321
					p.position = v.Position
					p.proposed = true
					proposal, _ := pc.EncodeSnapshots([]pc.Snapshot{v})
					sealed(i, pc.Snapshots, proposal)
				}
				other := 1 - i
				if len(poses) > other && p.announced && peers[other].proposed && poses[other].Position == peers[other].position && poses[other].Yaw == 12345 && poses[other].Pitch == 54321 {
					p.sawPeer = true
				}
			}
		}
	}
	if peers[0].spawn == peers[1].spawn {
		t.Fatal("overlapping spawn identities")
	}
	clients[0].Close()
	read(1, lobby.MemberLeft)
	// The creator was a player, never the gameplay host. Slot one survives
	// with its original ticket/generation and receives an authoritative quit.
	quit, sparse := false, false
	peers[1].conn.SetReadDeadline(time.Now().Add(3 * time.Second))
	for !quit || !sparse {
		var buffer [4096]byte
		n, _, err := peers[1].conn.ReadFromUDPAddrPort(buffer[:])
		if err != nil {
			t.Fatal("server stopped with owner", err)
		}
		plain, err := peers[1].channel.Open(buffer[:n])
		if err != nil {
			t.Fatal(err)
		}
		kind, body, err := pc.Decode(plain)
		if err != nil {
			t.Fatal(err)
		}
		if kind == pc.Native {
			packet, err := game.Decode(body)
			if err != nil {
				t.Fatal(err)
			}
			for _, m := range packet.Messages {
				if m[0] == game.PlayerQuit && m[1] == 0 {
					quit = packet.Header.Reliable
				}
			}
		}
		if kind == pc.Snapshots {
			records, err := pc.DecodeSnapshots(body)
			if err != nil {
				t.Fatal(err)
			}
			sparse = len(records) == 1 && records[0].Slot == 1 && records[0].Position == peers[1].position && records[0].Generation == peers[1].generation
		}
	}
	clients[1].Close()
}
