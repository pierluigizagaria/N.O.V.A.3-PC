package game

import "encoding/binary"

const LaunchBullet byte = 24

// Shot is the original launch-bullet record. Coordinates remain in the native
// map quantization domain until the scene owner supplies the verified bounds.
// Decoding is not authorization: actor, weapon, clock, origin and fire cadence
// must be checked against server state before any damage is calculated.
type Shot struct {
	Time                    uint32
	Actor                   byte
	Origin, Direction       [3]uint16
	Weapon, Flags           byte
	Scalar                  uint16 // Present when Flags&2 != 0; native multiplier is 3276.75.
	SourceType, SourceIndex byte   // Present when Flags&8 != 0; absent type is 3.
}

func ReadShot(m []byte) (Shot, error) {
	if len(m) < 22 || len(m) > 26 || m[0] != LaunchBullet || int(binary.BigEndian.Uint16(m[1:])) != len(m) {
		return Shot{}, ErrPacket
	}
	v := Shot{Time: binary.BigEndian.Uint32(m[3:]), Actor: m[7], Weapon: m[20], Flags: m[21], SourceType: 3}
	if v.Flags & ^byte(31) != 0 {
		return Shot{}, ErrPacket
	}
	length := 22
	if v.Flags&2 != 0 {
		length += 2
	}
	if v.Flags&8 != 0 {
		length += 2
	}
	if length != len(m) {
		return Shot{}, ErrPacket
	}
	for i := 0; i < 3; i++ {
		v.Origin[i] = binary.BigEndian.Uint16(m[8+2*i:])
		v.Direction[i] = binary.BigEndian.Uint16(m[14+2*i:])
	}
	cursor := 22
	if v.Flags&2 != 0 {
		v.Scalar = binary.BigEndian.Uint16(m[cursor:])
		cursor += 2
	}
	if v.Flags&8 != 0 {
		v.SourceType, v.SourceIndex = m[cursor], m[cursor+1]
		// The original encoder omits the optional pair for source type 3.
		if v.SourceType == 3 {
			return Shot{}, ErrPacket
		}
	}
	return v, nil
}

func (v Shot) Message() ([]byte, error) {
	if (v.Flags&2 == 0 && v.Scalar != 0) || (v.Flags&8 == 0 && (v.SourceType != 3 || v.SourceIndex != 0)) {
		return nil, ErrPacket
	}
	b := make([]byte, 22)
	b[0] = LaunchBullet
	binary.BigEndian.PutUint32(b[3:], v.Time)
	b[7] = v.Actor
	for i := 0; i < 3; i++ {
		binary.BigEndian.PutUint16(b[8+2*i:], v.Origin[i])
		binary.BigEndian.PutUint16(b[14+2*i:], v.Direction[i])
	}
	b[20], b[21] = v.Weapon, v.Flags
	if v.Flags&2 != 0 {
		b = binary.BigEndian.AppendUint16(b, v.Scalar)
	}
	if v.Flags&8 != 0 {
		b = append(b, v.SourceType, v.SourceIndex)
	}
	binary.BigEndian.PutUint16(b[1:], uint16(len(b)))
	if _, err := ReadShot(b); err != nil {
		return nil, err
	}
	return b, nil
}
