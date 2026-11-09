package native

import (
	"crypto/rand"
	"encoding/binary"
	"errors"
	"time"

	"nova3.test/backend/internal/protocol/lobby"
	"nova3.test/backend/internal/rooms"
)

const socialControlOpcode uint32 = 0x4e335349
const socialCapability uint32 = 2
const socialEventCapability uint32 = 4

type gameInvitation struct {
	sender, id, name string
	room             uint32
	expires          time.Time
	next             time.Time
	event            [16]byte
	persist          func() error // Unpublished menu intent, until a room exists.
}

func invitationFrame(v gameInvitation, now time.Time, events bool) lobby.Frame {
	// This transport record is consumed by the PC adapter, before native lobby
	// readers. Public lobby identity is safe for the original 17-byte invite UID.
	b := make([]byte, 10, 42)
	b[0], b[1] = 1, 1
	binary.BigEndian.PutUint32(b[2:], v.room)
	binary.BigEndian.PutUint16(b[6:], uint16(max(1, min(300, int(v.expires.Sub(now)/time.Second)))))
	b[8], b[9] = byte(len(v.id)), byte(len(v.name))
	if events {
		b[0] = 2
		b = append(b, v.event[:]...)
	}
	b = append(b, v.id...)
	b = append(b, v.name...)
	return lobby.Frame{Opcode: socialControlOpcode, Payload: b}
}

func (s *roomService) pruneInvitations(now time.Time) {
	for recipient, entries := range s.invitations {
		for sender, v := range entries {
			r, err := s.registry.Current(sender)
			if !now.Before(v.expires) || s.peers[sender] == nil ||
				(v.room != 0 && (err != nil || r.ID != v.room)) {
				delete(entries, sender)
			}
		}
		if len(entries) == 0 {
			delete(s.invitations, recipient)
		}
	}
}

func (s *roomService) invite(owner, target string, now time.Time, persist func() error) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.pruneInvitations(now)
	p := s.peers[owner]
	r, err := s.registry.Current(owner)
	if p == nil || owner == target || (err != nil && !errors.Is(err, rooms.ErrMissing)) ||
		(err == nil && len(r.Players) >= r.Settings.Capacity) {
		return errors.New("invitation requires an online inviter and available room")
	}
	if recipient := s.peers[target]; recipient != nil && !recipient.social {
		return errors.New("recipient needs the updated invitation client")
	}
	if v, exists := s.invitations[target][owner]; exists && now.Before(v.next) {
		// The original reliable/repeated UI action must not send duplicates.
		return nil
	}
	if len(s.invitations[target]) >= 8 || (s.invitations[target] == nil && len(s.invitations) >= 1024) {
		return errors.New("too many pending invitations")
	}
	var event [16]byte
	if _, err := rand.Read(event[:]); err != nil {
		return err
	}
	if r.ID != 0 {
		if err := persist(); err != nil {
			return err
		}
	}
	if s.invitations == nil {
		s.invitations = make(map[string]map[string]gameInvitation)
	}
	if s.invitations[target] == nil {
		s.invitations[target] = make(map[string]gameInvitation)
	}
	v := gameInvitation{sender: owner, id: p.player.ID, name: p.player.Name, room: r.ID,
		expires: now.Add(5 * time.Minute), next: now.Add(30 * time.Second), event: event}
	if r.ID == 0 {
		v.persist = persist
	}
	s.invitations[target][owner] = v
	// The menu action precedes map selection/room creation. Retain it without
	// notifying the recipient until the native lobby actually exists.
	if peer := s.peers[target]; peer != nil && v.room != 0 {
		peer.send(invitationFrame(v, now, peer.socialEvents))
	}
	return nil
}

func (s *roomService) invitationLocation(recipient, inviter string, now time.Time) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.pruneInvitations(now)
	for _, v := range s.invitations[recipient] {
		if v.id == inviter {
			r, err := s.registry.Current(v.sender)
			if err == nil && r.ID == v.room && len(r.Players) < r.Settings.Capacity {
				return nil
			}
		}
	}
	return errors.New("invitation expired or room unavailable")
}

func (s *roomService) replayInvitations(p *roomPeer, now time.Time) {
	s.pruneInvitations(now)
	if !p.social {
		return
	}
	for _, v := range s.invitations[p.player.Account] {
		if v.room != 0 {
			p.send(invitationFrame(v, now, p.socialEvents))
		}
	}
}

// A menu invitation waits for the inviter to finish the original map/mode
// selection and create the room. Bind and deliver that same event once, without
// inventing settings or alerting the recipient before there is a game to join.
// The room gate is held by the caller.
func (s *roomService) bindInvitations(owner string, room uint32, now time.Time) {
	s.pruneInvitations(now)
	r, err := s.registry.Current(owner)
	if err != nil || r.ID != room || len(r.Players) >= r.Settings.Capacity {
		return
	}
	for recipient, entries := range s.invitations {
		v, ok := entries[owner]
		if !ok || v.room != 0 {
			continue
		}
		if v.persist != nil {
			if err := v.persist(); err != nil {
				delete(entries, owner)
				continue
			}
			v.persist = nil
		}
		v.room = room
		// The original UI can submit77 again after completing map selection.
		// Measure its duplicate window from delivery, not the earlier menu click.
		v.next = now.Add(30 * time.Second)
		entries[owner] = v
		if p := s.peers[recipient]; p != nil && p.social {
			p.send(invitationFrame(v, now, p.socialEvents))
		}
	}
}
