package lobby

import (
	"encoding/binary"
	"errors"
	"net/netip"

	"nova3.test/backend/internal/protocol/pc"
	"nova3.test/backend/internal/protocol/secure"
)

// MatchControlPush is consumed by the PC transport before the original lobby
// reader. Only authenticated lobby connections may carry this secret payload.
const MatchControlPush uint32 = 0x4e335043

func GrantMatch(endpoint netip.AddrPort, ticket secure.Ticket) (Frame, error) {
	if !endpoint.IsValid() || !endpoint.Addr().Is4() || endpoint.Port() == 0 ||
		endpoint.Addr().IsUnspecified() || endpoint.Addr().IsMulticast() || endpoint.Addr().String() == "255.255.255.255" {
		return Frame{}, errors.New("match ticket requires a unicast IPv4 endpoint")
	}
	body := make([]byte, 92)
	body[0], body[1] = 1, 1
	ip := endpoint.Addr().As4()
	copy(body[2:6], ip[:])
	binary.BigEndian.PutUint16(body[6:8], endpoint.Port())
	binary.BigEndian.PutUint32(body[8:12], pc.SnapshotV1)
	copy(body[12:28], ticket.ID[:])
	copy(body[28:60], ticket.ClientKey[:])
	copy(body[60:92], ticket.ServerKey[:])
	return Frame{Opcode: MatchControlPush, Payload: body}, nil
}

// Revocation identifies the exact allocation so delayed teardown cannot clear
// a newer match. It is carried over the same ordered TLS lobby connection.
func RevokeMatch(id [16]byte) Frame {
	body := make([]byte, 18)
	body[0], body[1] = 1, 2
	copy(body[2:], id[:])
	return Frame{Opcode: MatchControlPush, Payload: body}
}
