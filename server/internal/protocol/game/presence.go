package game

import "encoding/binary"

const (
	NewPlayer              byte = 19
	SceneReady             byte = 46
	StartPowerAnimation    byte = 49
	DetonateMine           byte = 51
	FireStopSound          byte = 54
	RequestEnterVehicle    byte = 87
	RequestEnterMachinegun byte = 127
	ExitMachinegun         byte = 129
	JetpackStart           byte = 151
	JetpackStop            byte = 152
)

// Presence is the original host's type-19 announcement. It activates and
// initializes the existing remote player object; publishing poses alone does
// not make a player visible. Slot/device/name are owned by the reservation.
type Presence struct {
	Player Player
	Spawn  uint32
}

func (p Presence) Message() ([]byte, error) {
	if p.Player.Slot >= 12 || !validName(p.Player.Name) || p.Spawn > 0x7fffffff {
		return nil, ErrPacket
	}
	b := make([]byte, 44)
	// Type 19 carries slot, record ID and device. Unlike type 56, it has no
	// status byte: the receiver activates the slot unconditionally.
	b[0], b[1], b[2], b[3] = NewPlayer, p.Player.Slot, p.Player.Slot, p.Player.Slot
	copy(b[4:20], p.Player.Beacon[:])
	copy(b[20:40], p.Player.Name)
	binary.BigEndian.PutUint32(b[40:], p.Spawn)
	return b, nil
}

func ReadPresence(b []byte) (Presence, error) {
	if len(b) != 44 || b[0] != NewPlayer || b[1] >= 12 || b[2] >= 12 || b[3] >= 32 || binary.BigEndian.Uint32(b[40:]) > 0x7fffffff {
		return Presence{}, ErrPacket
	}
	name, err := readName(b[20:40])
	if err != nil {
		return Presence{}, err
	}
	p := Presence{Player: Player{Slot: b[1], Name: name}, Spawn: binary.BigEndian.Uint32(b[40:])}
	copy(p.Player.Beacon[:], b[4:20])
	return p, nil
}
