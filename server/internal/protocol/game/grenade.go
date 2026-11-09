package game

import (
	"encoding/binary"
	"math"
)

const (
	ThrowGrenade          byte = 26
	ThrowGrenadeWithSpeed byte = 27
	MeleeAttack           byte = 62
)

// GrenadeThrow retains quantized position and two signed unit-vector domains.
// Grenade is an index in the map's grenade object registry, not a template ID.
// Weapon is the separate native weapon-table index. Speed==0 uses the original
// template/movement-dependent speed; type27 carries a positive explicit speed.
type GrenadeThrow struct {
	Actor                       byte
	Time                        uint32
	Origin, Direction, Movement [3]uint16
	Grenade, Weapon             byte
	Speed                       float32
}

func (v GrenadeThrow) Message() ([]byte, error) {
	if math.IsNaN(float64(v.Speed)) || math.IsInf(float64(v.Speed), 0) || v.Speed < 0 {
		return nil, ErrPacket
	}
	b := make([]byte, 26)
	b[0], b[1], b[24], b[25] = ThrowGrenade, v.Actor, v.Grenade, v.Weapon
	binary.BigEndian.PutUint32(b[2:], v.Time)
	for i := 0; i < 3; i++ {
		binary.BigEndian.PutUint16(b[6+2*i:], v.Origin[i])
		binary.BigEndian.PutUint16(b[12+2*i:], v.Direction[i])
		binary.BigEndian.PutUint16(b[18+2*i:], v.Movement[i])
	}
	if v.Speed > 0 {
		b[0] = ThrowGrenadeWithSpeed
		// GenericDataPacket floats preserve x86 byte order, unlike integers.
		b = binary.LittleEndian.AppendUint32(b, math.Float32bits(v.Speed))
	}
	if _, err := ReadGrenadeThrow(b); err != nil {
		return nil, err
	}
	return b, nil
}

func ReadGrenadeThrow(b []byte) (GrenadeThrow, error) {
	if len(b) != 26 && len(b) != 30 {
		return GrenadeThrow{}, ErrPacket
	}
	if b[1] >= 12 || (b[0] != ThrowGrenade && b[0] != ThrowGrenadeWithSpeed) || (b[0] == ThrowGrenade) != (len(b) == 26) {
		return GrenadeThrow{}, ErrPacket
	}
	v := GrenadeThrow{Actor: b[1], Time: binary.BigEndian.Uint32(b[2:]), Grenade: b[24], Weapon: b[25]}
	for i := 0; i < 3; i++ {
		v.Origin[i] = binary.BigEndian.Uint16(b[6+2*i:])
		v.Direction[i] = binary.BigEndian.Uint16(b[12+2*i:])
		v.Movement[i] = binary.BigEndian.Uint16(b[18+2*i:])
	}
	if len(b) == 30 {
		v.Speed = math.Float32frombits(binary.LittleEndian.Uint32(b[26:]))
		if !(v.Speed > 0) || math.IsInf(float64(v.Speed), 0) {
			return GrenadeThrow{}, ErrPacket
		}
	}
	return v, nil
}

// MeleeRequest has no sender slot. The original receiver resolves the attacker
// from the authenticated transport device; the encoded slot is the victim.
type MeleeRequest struct {
	Time   uint32
	Victim byte
	Origin [3]uint16
	Weapon byte
}

func (v MeleeRequest) Message() ([]byte, error) {
	b := make([]byte, 13)
	b[0], b[5], b[12] = MeleeAttack, v.Victim, v.Weapon
	binary.BigEndian.PutUint32(b[1:], v.Time)
	for i := 0; i < 3; i++ {
		binary.BigEndian.PutUint16(b[6+2*i:], v.Origin[i])
	}
	if _, err := ReadMeleeRequest(b); err != nil {
		return nil, err
	}
	return b, nil
}

func ReadMeleeRequest(b []byte) (MeleeRequest, error) {
	if len(b) != 13 || b[0] != MeleeAttack || b[5] >= 12 {
		return MeleeRequest{}, ErrPacket
	}
	v := MeleeRequest{Time: binary.BigEndian.Uint32(b[1:]), Victim: b[5], Weapon: b[12]}
	for i := 0; i < 3; i++ {
		v.Origin[i] = binary.BigEndian.Uint16(b[6+2*i:])
	}
	return v, nil
}
