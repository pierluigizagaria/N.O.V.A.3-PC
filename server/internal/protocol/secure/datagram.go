// Package secure wraps unchanged native datagrams in authenticated encryption.
// Credentials and keys travel over the authenticated TLS control connection;
// neither user names nor UDP source addresses authorize a player.
package secure

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"encoding/binary"
	"errors"
	"math"
	"nova3.test/backend/internal/protocol/pc"
	"sync"
)

const HeaderSize = 28
const MaxPayload = pc.MaxPayload + pc.HeaderSize

var ErrDatagram = errors.New("invalid authenticated datagram")

// Ticket is secret control-plane material, not an operator setting or a native
// packet field. Fresh independent direction keys avoid reflected-packet/nonce
// reuse across client and server. Never log or persist a ticket.
type Ticket struct {
	ID                   [16]byte
	ClientKey, ServerKey [32]byte
}

func NewTicket() (Ticket, error) {
	var t Ticket
	for _, b := range [][]byte{t.ID[:], t.ClientKey[:], t.ServerKey[:]} {
		if _, err := rand.Read(b); err != nil {
			return Ticket{}, err
		}
	}
	return t, nil
}

type Channel struct {
	mu                  sync.Mutex
	id                  [16]byte
	send, receive       cipher.AEAD
	next, highest, seen uint64
}

func New(t Ticket, server bool) (*Channel, error) {
	sendKey, receiveKey := t.ClientKey, t.ServerKey
	if server {
		sendKey, receiveKey = receiveKey, sendKey
	}
	makeAEAD := func(k [32]byte) (cipher.AEAD, error) {
		block, err := aes.NewCipher(k[:])
		if err != nil {
			return nil, err
		}
		return cipher.NewGCM(block)
	}
	send, err := makeAEAD(sendKey)
	if err != nil {
		return nil, err
	}
	receive, err := makeAEAD(receiveKey)
	if err != nil {
		return nil, err
	}
	return &Channel{id: t.ID, send: send, receive: receive, next: 1}, nil
}

func ID(data []byte) ([16]byte, error) {
	var id [16]byte
	if len(data) < HeaderSize+16+1 || len(data) > HeaderSize+16+MaxPayload || string(data[:4]) != "NV3\x01" {
		return id, ErrDatagram
	}
	copy(id[:], data[4:20])
	return id, nil
}

func (c *Channel) Seal(payload []byte) ([]byte, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if len(payload) == 0 || len(payload) > MaxPayload || c.next == math.MaxUint64 {
		return nil, ErrDatagram
	}
	header := make([]byte, HeaderSize)
	copy(header, "NV3\x01")
	copy(header[4:], c.id[:])
	binary.BigEndian.PutUint64(header[20:], c.next)
	var nonce [12]byte
	binary.BigEndian.PutUint64(nonce[4:], c.next)
	c.next++
	return c.send.Seal(header, nonce[:], payload, header), nil
}

// Open admits reordered packets inside a 64-packet window exactly once. The
// replay window changes only after the authentication tag has been verified.
func (c *Channel) Open(data []byte) ([]byte, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	id, err := ID(data)
	if err != nil || id != c.id {
		return nil, ErrDatagram
	}
	seq := binary.BigEndian.Uint64(data[20:28])
	if seq == 0 || (seq <= c.highest && (c.highest-seq >= 64 || c.seen&(uint64(1)<<(c.highest-seq)) != 0)) {
		return nil, ErrDatagram
	}
	var nonce [12]byte
	binary.BigEndian.PutUint64(nonce[4:], seq)
	payload, err := c.receive.Open(nil, nonce[:], data[HeaderSize:], data[:HeaderSize])
	if err != nil {
		return nil, ErrDatagram
	}
	if seq > c.highest {
		c.seen <<= seq - c.highest
		c.highest = seq
	}
	c.seen |= uint64(1) << (c.highest - seq)
	return payload, nil
}
