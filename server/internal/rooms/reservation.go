package rooms

// Reservation pins one exact membership/settings snapshot while a match is
// allocated. It is an internal capability, never a client-supplied room ID.
// A departure cancels pre-game allocation. After Start, only the departing
// member loses authorization; the independent server keeps the session alive.
type Reservation struct {
	registry *Registry
	room     Room
	done     chan struct{}
	closed   bool // Protected by registry.mu.
	live     bool // Protected by registry.mu.
}

func (s *Registry) Reserve(owner string) (*Reservation, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	r := s.rooms[s.membership[owner]]
	if r == nil {
		return nil, ErrMissing
	}
	if r.Owner.Account != owner {
		return nil, ErrOwner
	}
	if r.reservation != nil {
		return nil, ErrBusy
	}
	lease := &Reservation{registry: s, room: snapshot(r), done: make(chan struct{})}
	r.reservation = lease
	return lease, nil
}

func (r *Reservation) Snapshot() Room        { return snapshot(&r.room) }
func (r *Reservation) Done() <-chan struct{} { return r.done }

func (r *Reservation) Member(account string) (Player, bool) {
	s := r.registry
	s.mu.Lock()
	defer s.mu.Unlock()
	room := s.rooms[r.room.ID]
	if r.closed || room == nil || room.reservation != r {
		return Player{}, false
	}
	for _, p := range room.Players {
		if p.Account == account {
			return p, true
		}
	}
	return Player{}, false
}

// Start commits the frozen allocation after every reserved player has loaded.
// The match owner can subsequently admit individual players without restarting
// the other scenes. Snapshot remains the immutable initial allocation.
func (r *Reservation) Start() bool {
	s := r.registry
	s.mu.Lock()
	defer s.mu.Unlock()
	room := s.rooms[r.room.ID]
	if r.closed || room == nil || room.reservation != r {
		return false
	}
	r.live = true
	return true
}

func (r *Reservation) Release() {
	s := r.registry
	s.mu.Lock()
	defer s.mu.Unlock()
	s.release(r)
}

// Must be called under mu. Pointer identity prevents a stale completion from
// releasing a subsequent allocation of the same lobby room.
func (s *Registry) release(r *Reservation) {
	if r == nil || r.closed {
		return
	}
	r.closed = true
	if room := s.rooms[r.room.ID]; room != nil && room.reservation == r {
		room.reservation = nil
	}
	close(r.done)
}
