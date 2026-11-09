package match

import (
	"net/netip"
	"time"

	"nova3.test/backend/internal/protocol/game"
	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/protocol/secure"
	"nova3.test/backend/internal/rooms"
)

// Admit adds one current lobby member to a running allocation. A new channel
// owns its entire handshake; neither the match epoch nor existing peers reset.
func (b *Bound) Admit(credential string, policy LoadoutPolicy, now time.Time) (secure.Ticket, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	if !b.active() || b.session.Snapshot().Phase != Playing {
		return secure.Ticket{}, ErrState
	}
	account, err := b.resolve(credential)
	if err != nil || b.byAccount[account] != nil || b.logical[account].IsValid() {
		return secure.Ticket{}, ErrAuthorization
	}
	member, ok := b.reservation.Member(account)
	if !ok {
		return secure.Ticket{}, ErrAuthorization
	}
	ticket, err := secure.NewTicket()
	if err != nil || b.byID[ticket.ID] != nil {
		return secure.Ticket{}, ErrAuthorization
	}
	channel, err := secure.New(ticket, true)
	if err != nil {
		return secure.Ticket{}, err
	}
	logical, err := b.session.admit(Member{Account: account, Name: member.Name}, policy, now)
	if err != nil {
		return secure.Ticket{}, err
	}
	p := &admittedPeer{account: account, credential: credential, logical: logical, expires: now.Add(15 * time.Minute), channel: channel}
	b.logical[account], b.byID[ticket.ID], b.byAccount[account], b.byLogical[logical] = logical, p, p, p
	return ticket, nil
}

func (b *Bound) CanAdmit() bool {
	b.mu.Lock()
	defer b.mu.Unlock()
	if !b.active() {
		return false
	}
	s := b.session
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.phase == Playing && s.movement != nil && s.admissionSlot() < int(s.loading.Capacity)
}

func (s *Session) admissionSlot() int {
	for i, p := range s.peers {
		if p.departed && s.slotReleased(byte(i)) {
			return i
		}
	}
	return len(s.peers)
}

func (s *Session) admit(member Member, policy LoadoutPolicy, now time.Time) (netip.AddrPort, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.phase != Playing || s.movement == nil {
		return netip.AddrPort{}, ErrState
	}
	slot := s.admissionSlot()
	for _, p := range s.peers {
		if !p.departed && p.member.Account == member.Account {
			return netip.AddrPort{}, ErrPeer
		}
	}
	if slot >= int(s.loading.Capacity) {
		return netip.AddrPort{}, rooms.ErrFull
	}
	member.Endpoint = netip.AddrPortFrom(netip.MustParseAddr("127.0.0.1"), uint16(slot+1))
	p := &peer{member: member, player: game.Player{Name: member.Name, Slot: byte(slot)}, lastReceived: now,
		admitting: Accepting, admissionDeadline: now.Add(15 * time.Second)}
	if _, err := p.player.Message(); err != nil {
		return netip.AddrPort{}, err
	}
	if slot < len(s.peers) {
		// Old tickets have already been retired before a slot becomes reusable.
		// Increment the life generation so delayed samples cannot revive it.
		s.retiredPeers = append(s.retiredPeers, s.peers[slot])
		s.peers[slot] = p
		s.movement.poses[slot].Generation++
		if s.movement.poses[slot].Generation == 0 {
			s.movement.poses[slot].Generation = 1
		}
	} else {
		s.peers = append(s.peers, p)
	}
	pose := &s.movement.poses[slot]
	spawn := s.movement.spawns[slot]
	pose.Position = [3]float32{float32(spawn.Transform.Position.X), float32(spawn.Transform.Position.Y), float32(spawn.Transform.Position.Z)}
	pose.Yaw, _ = pc.EncodeAngle(spawn.Transform.Rotation.Z)
	pose.Pitch, pose.Sequence = 0, 0
	s.movement.seen[slot], s.movement.upstream[slot] = false, 0
	if s.movement.loadouts != nil {
		s.movement.loadouts[member.Account] = policy
	}
	s.byEndpoint[member.Endpoint] = p
	return member.Endpoint, nil
}

// Native quit records have no life generation. Reuse only after every remaining
// scene acknowledged the old quit, so a delayed retry cannot delete a new actor.
func (s *Session) slotReleased(slot byte) bool {
	for _, p := range s.peers {
		if p.departed || p.transportFailed {
			continue
		}
		for _, pending := range p.channel.pending {
			packet, err := game.Decode(pending.data)
			if err != nil {
				return false
			}
			for _, message := range packet.Messages {
				if message[0] == game.PlayerQuit && message[1] == slot {
					return false
				}
			}
		}
	}
	return true
}

func (s *Session) loadNewcomer(p *peer, now time.Time, out *[]Delivery) error {
	roster, err := s.rosterMessage()
	if err != nil {
		return err
	}
	loading := s.loading
	loading.Player = p.player.Slot
	body, _ := loading.Message()
	p.admitting, p.admissionDeadline = Loading, now.Add(120*time.Second)
	// Keep the native roster/StartLoading ACK bootstrap in a single datagram.
	return s.emit(p, append(roster, body...), true, now, out)
}

func (s *Session) advanceAdmissions(now time.Time, out *[]Delivery) error {
	if s.phase != Playing {
		return nil
	}
	for _, p := range s.peers {
		if p.departed || p.transportFailed || p.admitting != Synchronizing {
			continue
		}
		if err := s.synchronizePeer(p, now, out); err != nil {
			return err
		}
		if p.clock.corrected && len(p.channel.pending) == 0 {
			if err := s.prepareSpawn(p); err != nil {
				return err
			}
			start := game.Start{RemainingMS: 2000, ElapsedMS: s.elapsed(now), Seed: s.movement.seed, Spawn: s.movement.spawns[p.player.Slot].ID}
			if err := s.emit(p, start.Message(), true, now, out); err != nil {
				return err
			}
			p.admitting, p.admissionDeadline = Playing, now.Add(peerTimeout)
		}
	}
	return nil
}
