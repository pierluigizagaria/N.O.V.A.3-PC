package game

import "encoding/binary"

const (
	ConsumePowerup     byte = 37
	PowerupSnapshot    byte = 39
	MapPowerupSnapshot byte = 40
)

// PowerupConsumption is a server grant, not a trusted client pickup request.
// Linked selects a child of the map object through the original resolver.
type PowerupConsumption struct {
	Actor  byte
	Object int32
	Linked byte
}

func (v PowerupConsumption) Message() ([]byte, error) {
	if v.Actor >= 12 {
		return nil, ErrPacket
	}
	b := []byte{ConsumePowerup, v.Actor, 0, 0, 0, 0, v.Linked}
	binary.BigEndian.PutUint32(b[2:], uint32(v.Object))
	return b, nil
}
func ReadPowerupConsumption(b []byte) (PowerupConsumption, error) {
	if len(b) != 7 || b[0] != ConsumePowerup || b[1] >= 12 {
		return PowerupConsumption{}, ErrPacket
	}
	return PowerupConsumption{b[1], int32(binary.BigEndian.Uint32(b[2:])), b[6]}, nil
}

// MapPowerupState controls an existing CMpPowerUpComponent's visibility. A map
// object ID and a player slot are different namespaces and must not be mixed.
type MapPowerupState struct {
	Object  int32
	Visible bool
}

func (v MapPowerupState) Message() []byte {
	b := []byte{MapPowerupSnapshot, 0, 0, 0, 0, 0}
	binary.BigEndian.PutUint32(b[1:], uint32(v.Object))
	if v.Visible {
		b[5] = 1
	}
	return b
}
func ReadMapPowerupState(b []byte) (MapPowerupState, error) {
	if len(b) != 6 || b[0] != MapPowerupSnapshot || b[5] > 1 {
		return MapPowerupState{}, ErrPacket
	}
	return MapPowerupState{int32(binary.BigEndian.Uint32(b[1:])), b[5] == 1}, nil
}

// PowerupState also describes dynamically created weapon/power pickups. Flags
// bit0 is visibility; bit1 and State preserve the dropped weapon's owner.
// Position uses native map bounds, not IEEE floats.
type PowerupState struct {
	Object       int32
	Flags, State byte
	Weapon       uint16
	Ammo         byte
	Power        uint16
	Position     [3]uint16
	Zone         int32
	Linked       byte
}

func (v PowerupState) Message() ([]byte, error) {
	if v.Flags > 3 || (v.Flags&2 == 0 && v.State != 0) {
		return nil, ErrPacket
	}
	b := make([]byte, 23)
	b[0] = PowerupSnapshot
	binary.BigEndian.PutUint32(b[1:], uint32(v.Object))
	b[5], b[6] = v.Flags, v.State
	binary.BigEndian.PutUint16(b[7:], v.Weapon)
	b[9] = v.Ammo
	binary.BigEndian.PutUint16(b[10:], v.Power)
	for i := range v.Position {
		binary.BigEndian.PutUint16(b[12+2*i:], v.Position[i])
	}
	binary.BigEndian.PutUint32(b[18:], uint32(v.Zone))
	b[22] = v.Linked
	return b, nil
}
func ReadPowerupState(b []byte) (PowerupState, error) {
	if len(b) != 23 || b[0] != PowerupSnapshot {
		return PowerupState{}, ErrPacket
	}
	v := PowerupState{Object: int32(binary.BigEndian.Uint32(b[1:])), Flags: b[5], State: b[6], Weapon: binary.BigEndian.Uint16(b[7:]), Ammo: b[9], Power: binary.BigEndian.Uint16(b[10:]), Zone: int32(binary.BigEndian.Uint32(b[18:])), Linked: b[22]}
	for i := range v.Position {
		v.Position[i] = binary.BigEndian.Uint16(b[12+2*i:])
	}
	if _, err := v.Message(); err != nil {
		return PowerupState{}, err
	}
	return v, nil
}
