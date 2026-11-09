package match

import (
	"errors"
	"net/netip"
	"sync"
	"time"

	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/protocol/secure"
	"nova3.test/backend/internal/rooms"
)

var ErrAuthorization = errors.New("match authorization unavailable")

// ResolveCredential must verify a revocable login credential and return its
// server-owned account identity. Passing a caller-provided UID is not valid.
type ResolveCredential func(credential string) (account string, err error)

type admittedPeer struct {
	account, credential string
	logical, remote     netip.AddrPort
	expires             time.Time
	channel             *secure.Channel
}

// Bound owns a session and one frozen lobby reservation. The control
// transport must call Issue over authenticated TLS; the UDP transport may pass
// untrusted datagrams to Handle. NewMovement enables the owner-approved preview;
// NewBound alone retains the independently tested pre-game boundary.
type Bound struct {
	mu          sync.Mutex
	reservation *rooms.Reservation
	resolve     ResolveCredential
	session     *Session
	byID        map[[16]byte]*admittedPeer
	byAccount   map[string]*admittedPeer
	byLogical   map[netip.AddrPort]*admittedPeer
	logical     map[string]netip.AddrPort
	closed      bool
	departures  []string
}

func NewBound(reservation *rooms.Reservation, options Options, resolve ResolveCredential, now time.Time) (*Bound, error) {
	if reservation == nil || resolve == nil || len(options.Members) != 0 {
		return nil, ErrAuthorization
	}
	r := reservation.Snapshot()
	if _, ok := reservation.Member(r.Owner.Account); !ok {
		return nil, ErrAuthorization
	}
	b := &Bound{reservation: reservation, resolve: resolve, byID: make(map[[16]byte]*admittedPeer), byAccount: make(map[string]*admittedPeer), byLogical: make(map[netip.AddrPort]*admittedPeer), logical: make(map[string]netip.AddrPort)}
	for i, p := range r.Players {
		// Internal routing keys only. They are never bound to sockets, exposed
		// as endpoints, or used to establish identity from a source address.
		logical := netip.AddrPortFrom(netip.AddrFrom4([4]byte{127, 0, 0, 1}), uint16(i+1))
		b.logical[p.Account] = logical
		options.Members = append(options.Members, Member{Account: p.Account, Name: p.Name, Endpoint: logical})
	}
	var err error
	b.session, err = New(options, now)
	if err != nil {
		return nil, err
	}
	return b, nil
}

func (b *Bound) Done() <-chan struct{} { return b.reservation.Done() }

// Issue returns fresh direction keys exactly once per account/allocation.
// Room membership is checked after credential verification; knowing room IDs,
// display names or a different valid account credential is insufficient.
func (b *Bound) Issue(credential string, capabilities uint32, now time.Time) (secure.Ticket, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	if !b.active() || capabilities != pc.SnapshotV1 {
		return secure.Ticket{}, ErrAuthorization
	}
	account, err := b.resolve(credential)
	if err != nil {
		return secure.Ticket{}, ErrAuthorization
	}
	if _, ok := b.reservation.Member(account); !ok || b.byAccount[account] != nil || !b.logical[account].IsValid() {
		return secure.Ticket{}, ErrAuthorization
	}
	t, err := secure.NewTicket()
	if err != nil {
		return secure.Ticket{}, err
	}
	if b.byID[t.ID] != nil {
		return secure.Ticket{}, ErrAuthorization
	}
	c, err := secure.New(t, true)
	if err != nil {
		return secure.Ticket{}, err
	}
	p := &admittedPeer{account: account, credential: credential, logical: b.logical[account], expires: now.Add(15 * time.Minute), channel: c}
	b.byID[t.ID], b.byAccount[account], b.byLogical[p.logical] = p, p, p
	return t, nil
}

func (b *Bound) valid(p *admittedPeer, now time.Time) bool {
	if !now.Before(p.expires) {
		return false
	}
	account, err := b.resolve(p.credential)
	if err != nil || account != p.account {
		return false
	}
	_, ok := b.reservation.Member(account)
	return ok
}

func (b *Bound) active() bool {
	if b.closed {
		return false
	}
	select {
	case <-b.reservation.Done():
		b.close()
		return false
	default:
		return true
	}
}

func (b *Bound) close() {
	if b.closed {
		return
	}
	b.closed = true
	b.session.Close()
	for _, p := range b.byID {
		p.credential = ""
		p.channel = nil
	}
	clear(b.byID)
	clear(b.byAccount)
	clear(b.byLogical)
	b.reservation.Release()
}

func (b *Bound) Close() { b.mu.Lock(); defer b.mu.Unlock(); b.close() }

// DrainDepartures hands completed removals to the lobby owner. Callers must
// notify the lobby outside the match hub lock to preserve lock ordering.
func (b *Bound) DrainDepartures() []string {
	b.mu.Lock()
	defer b.mu.Unlock()
	departures := b.departures
	b.departures = nil
	return departures
}
func (b *Bound) Snapshot() Snapshot {
	b.mu.Lock()
	defer b.mu.Unlock()
	b.active()
	return b.session.Snapshot()
}

func (b *Bound) RequestLoading() error {
	b.mu.Lock()
	defer b.mu.Unlock()
	if !b.active() {
		return ErrAuthorization
	}
	return b.session.RequestLoading()
}

func (b *Bound) encode(out []Delivery) ([]Delivery, error) {
	var encrypted []Delivery
	for _, d := range out {
		p := b.byLogical[d.To]
		if p == nil || !p.remote.IsValid() {
			b.close()
			return nil, ErrAuthorization
		}
		payload, err := pc.Encode(pc.Native, d.Data)
		if err != nil {
			b.close()
			return nil, err
		}
		data, err := p.channel.Seal(payload)
		if err != nil {
			b.close()
			return nil, err
		}
		encrypted = append(encrypted, Delivery{To: p.remote, Data: data})
	}
	return encrypted, nil
}

func (b *Bound) Handle(from netip.AddrPort, data []byte, now time.Time) ([]Delivery, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	if !b.active() {
		return nil, ErrAuthorization
	}
	if !from.IsValid() || from.Port() == 0 || !from.Addr().Is4() || from.Addr().IsUnspecified() || from.Addr().IsMulticast() || from.Addr().String() == "255.255.255.255" {
		return nil, ErrPeer
	}
	id, err := secure.ID(data)
	if err != nil {
		return nil, err
	}
	p := b.byID[id]
	if p == nil {
		return nil, ErrAuthorization
	}
	plain, err := p.channel.Open(data)
	if err != nil {
		return nil, err
	}
	if !b.valid(p, now) {
		// Tick retires this participant, preserving a running dedicated match.
		return nil, ErrAuthorization
	}
	kind, payload, err := pc.Decode(plain)
	if err != nil {
		return nil, err
	}
	// Movement proposals have their own slot/generation/clock gate. Never pass
	// PC records to the native decoder, or treat client life records as authority.
	if kind == pc.Snapshots {
		records, err := pc.DecodeSnapshots(payload)
		if err != nil {
			return nil, err
		}
		if err = b.session.propose(p.logical, records, now); err != nil {
			return nil, err
		}
		p.remote = from
		p.expires = now.Add(15 * time.Minute)
		return nil, nil
	}
	if kind != pc.Native {
		return nil, ErrState
	}
	out, err := b.session.Handle(p.logical, payload, now)
	if err != nil {
		return nil, err
	}
	// A newly authenticated packet may update the return address after NAT
	// rebinding. Unauthenticated/replayed/malformed packets cannot redirect it.
	p.remote = from
	p.expires = now.Add(15 * time.Minute)
	return b.encode(out)
}

func (b *Bound) Tick(now time.Time) ([]Delivery, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	if !b.active() {
		return nil, ErrAuthorization
	}
	var retired []netip.AddrPort
	unavailable := b.session.unavailable(now)
	for id, p := range b.byID {
		if unavailable[p.logical] || !b.valid(p, now) {
			phase := b.session.Snapshot().Phase
			if phase != Playing && phase != Finishing {
				b.close()
				return nil, ErrAuthorization
			}
			retired = append(retired, p.logical)
			b.departures = append(b.departures, p.account)
			delete(b.byID, id)
			delete(b.byLogical, p.logical)
			delete(b.byAccount, p.account)
			delete(b.logical, p.account)
			p.credential, p.channel = "", nil
		}
	}
	var departures []Delivery
	if len(retired) != 0 {
		if len(b.byID) == 0 {
			b.close()
			return nil, ErrAuthorization
		}
		var err error
		departures, err = b.session.retire(retired, now)
		if err != nil {
			b.close()
			return nil, err
		}
	}
	out, err := b.session.Tick(now)
	if err != nil {
		b.close()
		return nil, err
	}
	if b.session.Snapshot().Phase == Playing && !b.reservation.Start() {
		b.close()
		return nil, ErrAuthorization
	}
	out = append(departures, out...)
	encrypted, err := b.encode(out)
	if err != nil {
		return nil, err
	}
	lives, poses, err := b.session.publication(now)
	if err != nil {
		return nil, err
	}
	if lives == nil {
		return encrypted, nil
	}
	for _, p := range b.byAccount {
		if !p.remote.IsValid() {
			continue
		}
		for _, record := range []struct {
			kind byte
			body []byte
		}{{pc.Lives, lives}, {pc.Snapshots, poses}} {
			plain, err := pc.Encode(record.kind, record.body)
			if err != nil {
				return nil, err
			}
			data, err := p.channel.Seal(plain)
			if err != nil {
				return nil, err
			}
			encrypted = append(encrypted, Delivery{To: p.remote, Data: data})
		}
	}
	return encrypted, nil
}
