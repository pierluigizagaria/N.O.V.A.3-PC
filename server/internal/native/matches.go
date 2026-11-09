package native

import (
	"context"
	"crypto/rand"
	"errors"
	"fmt"
	"log"
	"net"
	"net/netip"
	"strconv"
	"strings"
	"sync"
	"time"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/authority"
	"nova3.test/backend/internal/match"
	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/protocol/lobby"
	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/protocol/secure"
	"nova3.test/backend/internal/rooms"
)

type allocation struct {
	resultID        string
	results         []match.Result
	resultsCaptured bool
	resultsSaved    bool
	progressSaved   []match.Result
	nextCheckpoint  time.Time
	roomID          uint32
	stage           match.Snapshot
	udpSeen         bool
	errorLog        udpErrorLog
	nextMovementLog time.Time
	bound           *match.Bound
	tickets         map[string]secure.Ticket
	peers           map[string]*roomPeer
}

// One UDP port serves all rooms, so VPS operators need one firewall rule.
// Ticket IDs route packets; only Bound can authenticate or admit their bodies.
type matchHub struct {
	consumeConsumable func(string, byte) (bool, error)
	authority         *authority.Definitions
	primaryWeapons    map[byte]uint16
	secondaryWeapons  map[byte]uint16
	addons            map[byte]byte
	items             map[byte]uint16
	loadouts          func([]rooms.Player) (map[string]match.LoadoutPolicy, error)
	mu                sync.Mutex
	byTicket          map[[16]byte]*allocation
	active            map[*allocation]bool
	resolve           match.ResolveCredential
	port              uint16
	advertise         netip.Addr
	closed            bool
	log               *log.Logger
	onDeparture       func(uint32, *roomPeer)
	recordResults     func(string, []match.Result, bool) error
}

func (s *Services) PrepareMatches(conn *net.UDPConn, advertise netip.Addr) {
	s.rooms.matches = &matchHub{byTicket: make(map[[16]byte]*allocation), active: make(map[*allocation]bool),
		authority: s.Authority,
		port:      conn.LocalAddr().(*net.UDPAddr).AddrPort().Port(), advertise: advertise, log: s.Log,
		resolve:           func(token string) (string, error) { return accounts.Resolve(s.Store, token) },
		recordResults:     s.recordMatchResults,
		consumeConsumable: s.consumeConsumable,
		onDeparture:       s.rooms.retireMatchPeer}
	s.rooms.matches.primaryWeapons = make(map[byte]uint16)
	s.rooms.matches.secondaryWeapons = make(map[byte]uint16)
	s.rooms.matches.addons = make(map[byte]byte)
	s.rooms.matches.items = make(map[byte]uint16)
	if s.Shop != nil {
		s.rooms.matches.loadouts = s.matchLoadouts
		for _, item := range s.Shop.Groups["addons"] {
			if item.Supported && item.Native != nil && item.Native.Ordinal >= 0 && item.Native.Ordinal < 127 && item.Native.TypeID >= 1 && item.Native.TypeID <= 6 {
				s.rooms.matches.addons[byte(item.Native.Ordinal+1)] = byte(item.Native.TypeID)
			}
		}
		for group, target := range map[string]map[byte]uint16{"primary": s.rooms.matches.primaryWeapons, "secondary": s.rooms.matches.secondaryWeapons, "items": s.rooms.matches.items} {
			for _, item := range s.Shop.Groups[group] {
				if item.Supported && item.Native != nil && item.Native.Ordinal >= 0 && item.Native.Ordinal < 127 && item.Native.TypeID > 0 && item.Native.TypeID <= 65535 {
					// Native preset/beacon selections are one-based; zero is empty.
					// MPLoadoutComponent::SetLoadout subtracts one for table lookup.
					target[byte(item.Native.Ordinal+1)] = uint16(item.Native.TypeID)
				}
			}
		}
	}
}

func (s *Services) ServeMatches(ctx context.Context, conn *net.UDPConn) error {
	return match.RunMultiplexed(ctx, conn, s.rooms.matches)
}

// Native GS_OnlineCreate writes map, mode, two rule parameters, capacity,
// five flags, tournament and a final text field. StartLoading uses that same
// order except for the recipient slot. Keep menu selections as the source.
func movementOptions(r rooms.Room) (match.Options, error) {
	if !lobby.ValidGameData(r.Settings.Data) {
		return match.Options{}, match.ErrState
	}
	parts := strings.Split(r.Settings.Data, "|")
	var v [11]int64
	for i := range v {
		n, err := strconv.ParseInt(parts[i], 10, 32)
		if err != nil || n < 0 {
			return match.Options{}, match.ErrState
		}
		v[i] = n
	}
	if v[0] > 5 || v[1] > 6 || !match.SupportedMode(byte(v[1])) || v[4] != int64(r.Settings.Capacity) {
		return match.Options{}, fmt.Errorf("unsupported mode/map or mismatched capacity; received map=%d mode=%d capacity=%d room_capacity=%d: %w", v[0], v[1], v[4], r.Settings.Capacity, match.ErrState)
	}
	loading := game.Loading{Map: byte(v[0]), Mode: byte(v[1]), Parameter1: uint32(v[2]), Parameter2: uint32(v[3]), Capacity: byte(v[4])}
	for i := range loading.Flags {
		if v[i+5] > 1 {
			return match.Options{}, match.ErrState
		}
		loading.Flags[i] = byte(v[i+5])
	}
	return match.Options{Details: game.Details{Name: r.Owner.Name, Beacon: [4]byte{loading.Map, 0, loading.Capacity, loading.Mode}}, Loading: loading}, nil
}

func (h *matchHub) allocate(r *rooms.Reservation, peers map[string]*roomPeer, now time.Time) (*allocation, error) {
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.closed {
		return nil, match.ErrState
	}
	options, err := movementOptions(r.Snapshot())
	if err != nil {
		return nil, err
	}
	options.PrimaryWeapons = h.primaryWeapons
	options.Authority = h.authority
	options.ConsumeConsumable = h.consumeConsumable
	options.SecondaryWeapons = h.secondaryWeapons
	options.Addons = h.addons
	options.Items = h.items
	if h.loadouts != nil {
		options.Loadouts, err = h.loadouts(r.Snapshot().Players)
		if err != nil {
			return nil, err
		}
	}
	b, err := match.NewMovement(r, options, h.resolve, now)
	if err != nil {
		return nil, err
	}
	a := &allocation{resultID: rand.Text(), roomID: r.Snapshot().ID, bound: b, stage: b.Snapshot(), tickets: make(map[string]secure.Ticket), peers: make(map[string]*roomPeer)}
	for _, member := range r.Snapshot().Players {
		p := peers[member.Account]
		if p == nil || p.credential == "" {
			b.Close()
			return nil, match.ErrAuthorization
		}
		ticket, err := b.Issue(p.credential, pc.SnapshotV1, now)
		if err != nil {
			b.Close()
			return nil, err
		}
		a.tickets[member.Account], a.peers[member.Account] = ticket, p
	}
	for _, ticket := range a.tickets {
		if h.byTicket[ticket.ID] != nil {
			b.Close()
			return nil, match.ErrAuthorization
		}
	}
	for _, ticket := range a.tickets {
		h.byTicket[ticket.ID] = a
	}
	h.active[a] = true
	return a, nil
}

func (h *matchHub) remove(a *allocation) {
	if !h.active[a] {
		return
	}
	if err := h.saveResults(a, time.Now()); err != nil {
		if h.log != nil {
			h.log.Printf("native match room=%d result save failed: %v", a.roomID, err)
		}
		return // Keep the immutable result for the next tick's retry.
	}
	delete(h.active, a)
	if h.log != nil {
		v := a.bound.Snapshot()
		h.log.Printf("native match room=%d movement final inputs=%v changes=%v snapshot_batches=%d combat=%+v", a.roomID, v.PositionInputs, v.PositionChanges, v.SnapshotBatches, v.Combat)
	}
	a.bound.Close()
	for account, ticket := range a.tickets {
		delete(h.byTicket, ticket.ID)
		a.peers[account].send(lobby.RevokeMatch(ticket.ID))
	}
	clear(a.tickets)
	clear(a.peers)
}

func (h *matchHub) Handle(from netip.AddrPort, data []byte, now time.Time) ([]match.Delivery, error) {
	h.mu.Lock()
	defer h.mu.Unlock()
	id, err := secure.ID(data)
	if err != nil {
		return nil, err
	}
	a := h.byTicket[id]
	if a == nil {
		return nil, match.ErrAuthorization
	}
	out, err := a.bound.Handle(from, data, now)
	if a.bound.Snapshot().Phase == match.Finishing {
		if saveErr := h.saveResults(a, now); saveErr != nil && h.log != nil {
			h.log.Printf("native match room=%d result save failed: %v", a.roomID, saveErr)
		}
	}
	if h.log != nil && !a.udpSeen {
		h.log.Printf("native match room=%d UDP received", a.roomID)
	}
	a.udpSeen = true
	if err != nil {
		a.errorLog.record(h.log, a.roomID, err, now)
	}
	if errors.Is(err, match.ErrTimeout) || errors.Is(err, match.ErrWindow) {
		h.remove(a)
		// A single expired room must never terminate the shared listener.
		return nil, nil
	}
	return out, err
}

func (h *matchHub) Tick(now time.Time) ([]match.Delivery, error) {
	h.mu.Lock()
	type departure struct {
		room uint32
		peer *roomPeer
	}
	var departures []departure
	defer func() {
		h.mu.Unlock()
		// Room launch acquires the room gate before this hub. Never invert
		// that order while removing stalled clients from a running match.
		if h.onDeparture != nil {
			for _, d := range departures {
				h.onDeparture(d.room, d.peer)
			}
		}
	}()
	var out []match.Delivery
	for a := range h.active {
		deliveries, err := a.bound.Tick(now)
		retired := a.bound.DrainDepartures()
		for _, account := range retired {
			if ticket, ok := a.tickets[account]; ok {
				peer := a.peers[account]
				delete(h.byTicket, ticket.ID)
				delete(a.tickets, account)
				delete(a.peers, account)
				peer.send(lobby.RevokeMatch(ticket.ID))
				departures = append(departures, departure{a.roomID, peer})
				if h.log != nil {
					h.log.Printf("native match room=%d participant retired", a.roomID)
				}
			}
		}
		stage := a.bound.Snapshot()
		if h.log != nil && stage.Spawns != a.stage.Spawns {
			h.log.Printf("native match room=%d selected spawns=%v", a.roomID, stage.Spawns)
		}
		if stage.Phase == match.Playing {
			if len(retired) != 0 {
				a.nextCheckpoint = time.Time{}
			}
			if saveErr := h.saveProgress(a, now); saveErr != nil && h.log != nil {
				h.log.Printf("native match room=%d progress save failed: %v", a.roomID, saveErr)
			}
		}
		if stage.Phase == match.Finishing {
			if saveErr := h.saveResults(a, now); saveErr != nil && h.log != nil {
				h.log.Printf("native match room=%d result save failed: %v", a.roomID, saveErr)
			}
		}
		if h.log != nil && stage.SnapshotBatches != 0 && (a.nextMovementLog.IsZero() || !now.Before(a.nextMovementLog)) {
			h.log.Printf("native match room=%d movement inputs=%v changes=%v snapshot_batches=%d combat=%+v", a.roomID, stage.PositionInputs, stage.PositionChanges, stage.SnapshotBatches, stage.Combat)
			a.nextMovementLog = now.Add(30 * time.Second)
		}
		if h.log != nil && stage.Active != a.stage.Active {
			h.log.Printf("native match room=%d scene active=%d/%d", a.roomID, stage.Active, stage.Reserved)
		}
		if h.log != nil && stage.Heartbeats != a.stage.Heartbeats {
			h.log.Printf("native match room=%d heartbeat confirmed=%d/%d", a.roomID, stage.Heartbeats, stage.Reserved)
		}
		if h.log != nil && (stage.Phase != a.stage.Phase || stage.Joined != a.stage.Joined || stage.Loaded != a.stage.Loaded || err != nil) {
			h.log.Printf("native match room=%d phase=%s joined=%d/%d loaded=%d result=%v", a.roomID, stage.Phase, stage.Joined, stage.Reserved, stage.Loaded, err)
		}
		a.stage = stage
		if err != nil {
			h.remove(a)
			continue
		}
		out = append(out, deliveries...)
		if stage.Phase == match.Finished {
			h.remove(a)
		}
	}
	return out, nil
}

func (h *matchHub) Close() {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.closed = true
	for a := range h.active {
		h.remove(a)
	}
}

// Called under the room gate. Grant precedes endpoint announcement on each
// ordered TLS stream; all tickets exist before any client can start connecting.
func (s *roomService) launch(p *roomPeer, r rooms.Room) bool {
	if s.matches == nil {
		return p.send(lobby.RoomStatus(lobby.LaunchResponse, 0x4080))
	}
	lease, err := s.registry.Reserve(p.player.Account)
	if err != nil {
		return p.send(lobby.RoomStatus(lobby.LaunchResponse, roomError(err)))
	}
	a, err := s.matches.allocate(lease, s.peers, time.Now())
	if err != nil {
		lease.Release()
		if s.matches.log != nil {
			s.matches.log.Printf("native match room=%d allocation rejected: %v", r.ID, err)
		}
		return p.send(lobby.RoomStatus(lobby.LaunchResponse, 0x4080))
	}
	if s.matches.log != nil {
		s.matches.log.Printf("native match room=%d allocated players=%d", r.ID, len(a.peers))
	}
	s.matches.mu.Lock()
	defer s.matches.mu.Unlock()
	if !s.matches.active[a] {
		return p.send(lobby.RoomStatus(lobby.LaunchResponse, 0x4080))
	}
	ok := p.send(lobby.RoomStatus(lobby.LaunchResponse, 0))
	for account, ticket := range a.tickets {
		peer := a.peers[account]
		address := s.matches.advertise
		if !address.IsValid() {
			address = peer.local.Addr()
		}
		endpoint := netip.AddrPortFrom(address, s.matches.port)
		grant, e1 := lobby.GrantMatch(endpoint, ticket)
		announcement, e2 := lobby.AnnounceGameEndpoint(lobby.GameEndpoint{Address: address.String(), FirstPort: peer.local.Port(), GamePort: endpoint.Port(), RoomID: r.ID})
		if e1 != nil || e2 != nil || !peer.send(grant) || !peer.send(announcement) {
			ok = false
		}
	}
	if !ok {
		s.matches.remove(a)
	}
	return ok
}
