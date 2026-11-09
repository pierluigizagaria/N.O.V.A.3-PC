// Package rooms owns transient room membership. It has no socket, codec or
// gameplay dependencies. Snapshots never expose mutable registry storage.
package rooms

import (
	"errors"
	"sort"
	"sync"
)

var (
	ErrMember   = errors.New("account already belongs to a room")
	ErrMissing  = errors.New("room does not exist")
	ErrFull     = errors.New("room is full")
	ErrCapacity = errors.New("invalid room capacity")
	ErrOwner    = errors.New("only the room owner can change settings")
	ErrBusy     = errors.New("room is reserved for a match")
)

type Player struct {
	Account, ID, Name, Data string
	Status                  byte
}
type Property struct {
	ID, Operator byte
	Value        string
}
type Settings struct {
	Type, Flags byte
	Capacity    int
	Data        string
	Version     uint32
	Properties  []Property
}
type Room struct {
	ID          uint32
	Owner       Player
	Settings    Settings
	Players     []Player
	reservation *Reservation
}
type Registry struct {
	mu         sync.Mutex
	next       uint32
	rooms      map[uint32]*Room
	membership map[string]uint32
}

// Reserved reports the state of this immutable room snapshot.
func (r Room) Reserved() bool { return r.reservation != nil }

func snapshot(r *Room) Room {
	v := *r
	v.Players = append([]Player(nil), r.Players...)
	v.Settings.Properties = append([]Property(nil), r.Settings.Properties...)
	return v
}
func (s *Registry) Create(p Player, settings Settings) (Room, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if settings.Capacity < 1 || settings.Capacity > 12 || p.Account == "" || p.ID == "" {
		return Room{}, ErrCapacity
	}
	if s.membership[p.Account] != 0 {
		return Room{}, ErrMember
	}
	if len(s.rooms) >= 64 || s.next >= 0x7fffffff {
		return Room{}, ErrFull
	}
	if s.rooms == nil {
		s.rooms = map[uint32]*Room{}
		s.membership = map[string]uint32{}
	}
	s.next++
	p.Status = 2
	settings.Properties = append([]Property(nil), settings.Properties...)
	r := &Room{ID: s.next, Owner: p, Settings: settings, Players: []Player{p}}
	s.rooms[r.ID] = r
	s.membership[p.Account] = r.ID
	return snapshot(r), nil
}
func (s *Registry) Join(p Player, id uint32) (Room, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.membership[p.Account] != 0 {
		return Room{}, ErrMember
	}
	r := s.rooms[id]
	if r == nil {
		return Room{}, ErrMissing
	}
	if r.reservation != nil && !r.reservation.live {
		return Room{}, ErrBusy
	}
	if len(r.Players) >= r.Settings.Capacity {
		return Room{}, ErrFull
	}
	// Preserve the admitted initial status requested by Join. The original
	// browser/Quickplay enters with 3; forcing 0 prevents its ready transition.
	r.Players = append(r.Players, p)
	s.membership[p.Account] = id
	return snapshot(r), nil
}
func (s *Registry) List() []Room {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := make([]Room, 0, len(s.rooms))
	for _, r := range s.rooms {
		if r.reservation != nil && !r.reservation.live {
			continue
		}
		out = append(out, snapshot(r))
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out
}
func (s *Registry) Current(account string) (Room, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	r := s.rooms[s.membership[account]]
	if r == nil {
		return Room{}, ErrMissing
	}
	return snapshot(r), nil
}

// Kick is a lobby mutation, admitted only for the owner before allocation.
// Returning the old roster lets the transport notify the removed peer too.
func (s *Registry) Kick(owner, targetID string) (Room, Player, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	r := s.rooms[s.membership[owner]]
	if r == nil {
		return Room{}, Player{}, ErrMissing
	}
	if r.Owner.Account != owner {
		return Room{}, Player{}, ErrOwner
	}
	if r.reservation != nil {
		return Room{}, Player{}, ErrBusy
	}
	if r.Owner.ID == targetID {
		return Room{}, Player{}, ErrOwner
	}
	for i, p := range r.Players {
		if p.ID == targetID {
			old := snapshot(r)
			r.Players = append(r.Players[:i], r.Players[i+1:]...)
			delete(s.membership, p.Account)
			return old, p, nil
		}
	}
	return Room{}, Player{}, ErrMissing
}

func (s *Registry) Get(id uint32) (Room, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	r := s.rooms[id]
	if r == nil {
		return Room{}, ErrMissing
	}
	return snapshot(r), nil
}

func (s *Registry) UpdatePlayer(account string, data *string, status *byte) (Room, Player, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	r := s.rooms[s.membership[account]]
	if r == nil {
		return Room{}, Player{}, ErrMissing
	}
	if r.reservation != nil {
		return Room{}, Player{}, ErrBusy
	}
	for i := range r.Players {
		p := &r.Players[i]
		if p.Account == account {
			if data != nil {
				p.Data = *data
			}
			if status != nil {
				p.Status = *status
			}
			if r.Owner.Account == account {
				r.Owner = *p
			}
			return snapshot(r), *p, nil
		}
	}
	return Room{}, Player{}, ErrMissing
}

func (s *Registry) UpdateSettings(account, data string, properties []Property) (Room, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	r := s.rooms[s.membership[account]]
	if r == nil {
		return Room{}, ErrMissing
	}
	if r.Owner.Account != account {
		return Room{}, ErrOwner
	}
	if r.reservation != nil {
		return Room{}, ErrBusy
	}
	r.Settings.Data = data
	for _, p := range properties {
		found := false
		for i := range r.Settings.Properties {
			if r.Settings.Properties[i].ID == p.ID {
				r.Settings.Properties[i] = p
				found = true
				break
			}
		}
		if !found {
			r.Settings.Properties = append(r.Settings.Properties, p)
		}
	}
	return snapshot(r), nil
}

// Leave removes the exact account. Before launch, an owner's departure closes
// the room. A running dedicated match survives departures and gives lobby
// ownership to the oldest remaining member; gameplay has no client host.
func (s *Registry) Leave(account string) (Room, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	r := s.rooms[s.membership[account]]
	if r == nil {
		return Room{}, false
	}
	old := snapshot(r)
	if r.reservation != nil && r.reservation.live && len(r.Players) > 1 {
		delete(s.membership, account)
		for i, p := range r.Players {
			if p.Account == account {
				r.Players = append(r.Players[:i], r.Players[i+1:]...)
				break
			}
		}
		if r.Owner.Account == account {
			r.Owner = r.Players[0]
		}
		return old, true
	}
	s.release(r.reservation)
	delete(s.membership, account)
	if r.Owner.Account == account {
		for _, p := range r.Players {
			delete(s.membership, p.Account)
		}
		delete(s.rooms, r.ID)
	} else {
		for i, p := range r.Players {
			if p.Account == account {
				r.Players = append(r.Players[:i], r.Players[i+1:]...)
				break
			}
		}
	}
	return old, true
}
