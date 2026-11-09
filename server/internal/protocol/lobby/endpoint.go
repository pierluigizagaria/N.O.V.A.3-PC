package lobby

import (
	"encoding/binary"
	"errors"
	"net/netip"
)

const GameEndpointPush uint32 = 50

// GameEndpoint is the original online handoff record. The first port's service
// role remains unproved; the second is consumed by Comms::InitServerAdress.
// Addresses here are resolved IPv4 literals, not operator configuration.
type GameEndpoint struct {
	Address             string
	FirstPort, GamePort uint16
	RoomID              uint32
}

// AnnounceGameEndpoint only encodes a record. Its caller must own a ready host
// and the matching room before sending it. Launch must not use this as a
// substitute for host allocation. There is no status word in this push body.
func AnnounceGameEndpoint(v GameEndpoint) (Frame, error) {
	ip, err := netip.ParseAddr(v.Address)
	if err != nil || !ip.Is4() || ip.IsUnspecified() || ip.IsMulticast() || v.Address == "255.255.255.255" {
		return Frame{}, errors.New("game endpoint requires a unicast IPv4 address")
	}
	if v.FirstPort == 0 || v.GamePort == 0 || v.RoomID == 0 {
		return Frame{}, errors.New("game endpoint requires both ports and a room ID")
	}
	// An IPv4 literal also bounds the native receiver's unbounded strcpy into
	// its 128-byte address buffer. Never pass arbitrary strings to that reader.
	b := String(v.Address)
	ports := [4]byte{}
	binary.BigEndian.PutUint16(ports[:2], v.FirstPort)
	binary.BigEndian.PutUint16(ports[2:], v.GamePort)
	b = append(b, ports[:]...)
	b = append(b, Integers(v.RoomID)...)
	return Frame{Opcode: GameEndpointPush, Payload: b}, nil
}
