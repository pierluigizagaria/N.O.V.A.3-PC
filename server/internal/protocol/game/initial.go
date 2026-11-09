package game

import "encoding/binary"

const (
	PlayersSnapshot byte = 56
	RequestPowerups byte = 60
)

// InitialPlayer is one of the twelve fixed records in the native type-56
// snapshot. It carries scene/scoreboard state, not the 30 Hz position stream.
type InitialPlayer struct {
	Player              Player
	Active              bool
	Kills, Deaths, Flag uint16
	AuxiliaryCounters   [3]uint16 // Native MpPlayer +0x60, +0x74, +0x80; semantics under review.
	Score               uint32
	Dead, HasPowerup    bool
	Frozen, WasFrozen   bool
	Weapon              uint16
	MountedObject       uint32
}

// InitialSnapshot uses zero flag positions/team scores for free-for-all. Other
// modes need their own authoritative objective state before using this codec.
type InitialSnapshot struct {
	RenderDelayMS uint32
	TeamScores    [2]uint16
	FlagPositions [2][3]uint16
	FlagObjects   [2]uint16
	Players       []InitialPlayer
}

func (s InitialSnapshot) Message() ([]byte, error) {
	if len(s.Players) > 12 {
		return nil, ErrPacket
	}
	b := make([]byte, 781)
	b[0] = PlayersSnapshot
	for i, position := range s.FlagPositions {
		for axis, value := range position {
			binary.BigEndian.PutUint16(b[1+i*6+axis*2:], value)
		}
		binary.BigEndian.PutUint16(b[13+i*2:], s.FlagObjects[i])
	}
	binary.BigEndian.PutUint16(b[17:], s.TeamScores[0])
	binary.BigEndian.PutUint16(b[19:], s.TeamScores[1])
	binary.BigEndian.PutUint32(b[21:], s.RenderDelayMS)
	for i := 0; i < 12; i++ {
		r := b[25+i*63 : 25+(i+1)*63]
		r[2], r[3] = 255, 255
	}
	seen := map[byte]bool{}
	for _, p := range s.Players {
		if p.Player.Slot >= 12 || seen[p.Player.Slot] || !validName(p.Player.Name) {
			return nil, ErrPacket
		}
		seen[p.Player.Slot] = true
		r := b[25+int(p.Player.Slot)*63 : 25+(int(p.Player.Slot)+1)*63]
		// The first byte is occupied, not a player/record ID. In particular,
		// slot zero must have occupied=1 or the native reader skips its row.
		r[0], r[2], r[3] = 1, p.Player.Slot, p.Player.Slot
		if p.Active {
			r[1] = 1
		}
		copy(r[4:20], p.Player.Beacon[:])
		copy(r[20:40], p.Player.Name)
		binary.BigEndian.PutUint16(r[40:], p.Kills)
		binary.BigEndian.PutUint16(r[42:], p.Deaths)
		binary.BigEndian.PutUint16(r[44:], p.Flag)
		binary.BigEndian.PutUint32(r[46:], p.Score)
		for i, v := range p.AuxiliaryCounters {
			binary.BigEndian.PutUint16(r[50+2*i:], v)
		}
		if p.Frozen {
			r[56] |= 1
		}
		if p.WasFrozen {
			r[56] |= 2
		}
		binary.BigEndian.PutUint16(r[57:], p.Weapon)
		binary.BigEndian.PutUint32(r[59:], p.MountedObject)
	}
	return b, nil
}
