package game

import "encoding/binary"

const ChangeZone byte = 107

// ZoneChange uses a per-player sequence in 0..253. Object IDs below 12 name
// player actors; other IDs name vehicles/NPCs and require separate authority.
type ZoneChange struct {
	Actor, Object byte
	Zone          int32
	Sequence      byte
}

func ReadZoneChange(m []byte) (ZoneChange, error) {
	if len(m) != 8 || m[0] != ChangeZone || m[1] >= 12 || m[7] > 253 {
		return ZoneChange{}, ErrPacket
	}
	v := ZoneChange{m[1], m[2], int32(binary.BigEndian.Uint32(m[3:])), m[7]}
	if v.Zone < 0 {
		return ZoneChange{}, ErrPacket
	}
	return v, nil
}

func (v ZoneChange) Message() ([]byte, error) {
	m := []byte{ChangeZone, v.Actor, v.Object, 0, 0, 0, 0, v.Sequence}
	binary.BigEndian.PutUint32(m[3:], uint32(v.Zone))
	_, err := ReadZoneChange(m)
	return m, err
}

// NewerZoneSequence applies the sender's modulo-254 counter. Equal and
// delayed/replayed samples cannot replace the current zone.
func NewerZoneSequence(next, previous byte) bool {
	return next <= 253 && previous <= 253 && next != previous && (int(next)-int(previous)+254)%254 < 127
}
