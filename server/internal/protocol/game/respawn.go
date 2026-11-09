package game

import "encoding/binary"

const (
	RespawnPlayer  byte = 31
	RequestRespawn byte = 32
)

// Respawn names a real map spawn object. The authority must establish that the
// actor is dead, its delay elapsed and the spawn is legal before emitting it.
type Respawn struct {
	Time  uint32
	Slot  byte
	Spawn uint32
}

func (r Respawn) Message() ([]byte, error) {
	if r.Slot >= 12 || r.Spawn > 0x7fffffff {
		return nil, ErrPacket
	}
	body := make([]byte, 10)
	body[0] = RespawnPlayer
	binary.BigEndian.PutUint32(body[1:], r.Time)
	body[5] = r.Slot
	binary.BigEndian.PutUint32(body[6:], r.Spawn)
	return body, nil
}

func ReadRespawn(body []byte) (Respawn, error) {
	if len(body) != 10 || body[0] != RespawnPlayer || body[5] >= 12 || binary.BigEndian.Uint32(body[6:]) > 0x7fffffff {
		return Respawn{}, ErrPacket
	}
	return Respawn{Time: binary.BigEndian.Uint32(body[1:]), Slot: body[5], Spawn: binary.BigEndian.Uint32(body[6:])}, nil
}

func RespawnRequest(slot byte) ([]byte, error) {
	if slot >= 12 {
		return nil, ErrPacket
	}
	return []byte{RequestRespawn, slot}, nil
}
