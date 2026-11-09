package native

import (
	"net/netip"
	"time"

	"nova3.test/backend/internal/match"
	"nova3.test/backend/internal/protocol/lobby"
	"nova3.test/backend/internal/rooms"
)

func (s *roomService) listedRooms() []rooms.Room {
	var listed []rooms.Room
	for _, room := range s.registry.List() {
		if room.Reserved() && !s.matches.joinable(room.ID) {
			continue
		}
		listed = append(listed, room)
	}
	return listed
}

func (h *matchHub) joinable(id uint32) bool {
	if h == nil {
		return false
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	for a := range h.active {
		if a.roomID == id {
			return a.bound.CanAdmit()
		}
	}
	return false
}

// Called under the room gate after membership was added. Failure rolls that
// membership back; success sends Join before the grant and native endpoint push.
func (h *matchHub) admit(r rooms.Room, p *roomPeer) ([]lobby.Frame, error) {
	if h == nil {
		return nil, rooms.ErrBusy
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.closed {
		return nil, rooms.ErrBusy
	}
	for a := range h.active {
		if a.roomID != r.ID || a.bound.Snapshot().Phase != match.Playing {
			continue
		}
		var policy match.LoadoutPolicy
		if h.loadouts != nil {
			policies, err := h.loadouts([]rooms.Player{p.player})
			if err != nil {
				return nil, err
			}
			var ok bool
			policy, ok = policies[p.player.Account]
			if !ok {
				return nil, match.ErrAuthorization
			}
		}
		ticket, err := a.bound.Admit(p.credential, policy, time.Now())
		if err != nil {
			return nil, err
		}
		a.tickets[p.player.Account], a.peers[p.player.Account] = ticket, p
		h.byTicket[ticket.ID] = a
		address := h.advertise
		if !address.IsValid() {
			address = p.local.Addr()
		}
		endpoint := netip.AddrPortFrom(address, h.port)
		grant, err := lobby.GrantMatch(endpoint, ticket)
		if err != nil {
			return nil, err
		}
		announcement, err := lobby.AnnounceGameEndpoint(lobby.GameEndpoint{Address: address.String(), FirstPort: p.local.Port(), GamePort: endpoint.Port(), RoomID: r.ID})
		if err != nil {
			return nil, err
		}
		if h.log != nil {
			h.log.Printf("native match room=%d admitting player", r.ID)
		}
		return []lobby.Frame{grant, announcement}, nil
	}
	return nil, rooms.ErrBusy
}
