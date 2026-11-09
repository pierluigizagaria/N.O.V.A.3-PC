// Package game implements the original 1.0.7 game transport, independently of
// the original library. Session state separately controls message admission.
package game

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
)

const (
	HeaderSize             = 13
	MaxDatagram            = 2048
	Compatibility   uint32 = 0x0ab25828
	Discover        byte   = 1
	ServerDetails   byte   = 2
	ClientDetails   byte   = 3
	ServerFull      byte   = 4
	Roster          byte   = 5
	RequestClock    byte   = 6
	ClockReply      byte   = 7
	SetClock        byte   = 8
	PlayerQuit      byte   = 12
	Ping            byte   = 13
	Acknowledge     byte   = 15
	StartLoading    byte   = 16
	FinishedLoading byte   = 17
	StartGame       byte   = 18
	EndGame         byte   = 38
)

var ErrPacket = errors.New("invalid or unsupported game datagram")

// The header words are little-endian; payload integers are big-endian.
// Bit zero of AckMask acknowledges Ack itself (not Ack-1).
type Header struct {
	Reliable               bool
	Sequence, Ack, AckMask uint32
}

type Packet struct {
	Header   Header
	Messages [][]byte // Each message includes its type and any length prefix.
}

func Decode(data []byte) (Packet, error) {
	if len(data) < HeaderSize+1 || len(data) > MaxDatagram || data[0] > 1 {
		return Packet{}, ErrPacket
	}
	p := Packet{Header: Header{data[0] == 1, binary.LittleEndian.Uint32(data[1:]), binary.LittleEndian.Uint32(data[5:]), binary.LittleEndian.Uint32(data[9:])}}
	for rest := data[HeaderSize:]; len(rest) > 0; {
		// Twelve-player admission may include 11 announcements, a snapshot,
		// 11 zones and 11 aim records. Keep a bound above that native batch.
		if len(p.Messages) == 64 {
			return Packet{}, ErrPacket
		}
		n := RecordSize(rest[0])
		if n == 0 {
			return Packet{}, fmt.Errorf("%w: unknown type=%d offset=%d", ErrPacket, rest[0], len(data)-len(rest))
		}
		if n == variableRecord {
			if len(rest) < 3 {
				return Packet{}, ErrPacket
			}
			n = int(binary.BigEndian.Uint16(rest[1:3]))
			if n < 3 || n > MaxDatagram-HeaderSize {
				return Packet{}, ErrPacket
			}
		}
		if n > len(rest) {
			return Packet{}, fmt.Errorf("%w: truncated type=%d size=%d remaining=%d", ErrPacket, rest[0], n, len(rest))
		}
		m := rest[:n]
		if err := validate(m); err != nil {
			return Packet{}, err
		}
		p.Messages = append(p.Messages, bytes.Clone(m))
		rest = rest[n:]
	}
	return p, nil
}

func Encode(h Header, messages ...[]byte) ([]byte, error) {
	b := make([]byte, HeaderSize)
	if h.Reliable {
		b[0] = 1
	}
	binary.LittleEndian.PutUint32(b[1:], h.Sequence)
	binary.LittleEndian.PutUint32(b[5:], h.Ack)
	binary.LittleEndian.PutUint32(b[9:], h.AckMask)
	for _, m := range messages {
		if len(m) == 0 || len(b)+len(m) > MaxDatagram {
			return nil, ErrPacket
		}
		b = append(b, m...)
	}
	if _, err := Decode(b); err != nil {
		return nil, err
	}
	return b, nil
}

func validName(name string) bool {
	return len(name) > 0 && len(name) <= 19 && !bytes.ContainsAny([]byte(name), "\x00\r\n")
}

func readName(raw []byte) (string, error) {
	if len(raw) != 20 {
		return "", ErrPacket
	}
	n := bytes.IndexByte(raw, 0)
	if n < 1 || !validName(string(raw[:n])) {
		return "", ErrPacket
	}
	return string(raw[:n]), nil
}

func validate(m []byte) error {
	switch m[0] {
	case ConsumePowerup:
		_, err := ReadPowerupConsumption(m)
		return err
	case PowerupSnapshot:
		_, err := ReadPowerupState(m)
		return err
	case MapPowerupSnapshot:
		_, err := ReadMapPowerupState(m)
		return err
	case DamagePlayer:
		_, err := ReadDamage(m)
		return err
	case HitPlayer, HitObject:
		_, err := ReadHit(m)
		return err
	case PlayerDeath:
		_, err := ReadDeath(m)
		return err
	case PlayerQuit:
		if m[1] >= 12 {
			return ErrPacket
		}
	case RequestConsumable, ActivateConsumable:
		_, err := ReadConsumable(m)
		return err
	case PresetChat:
		_, err := ReadChat(m)
		return err
	case ChangeZone:
		_, err := ReadZoneChange(m)
		return err
	case NewPlayer:
		_, err := ReadPresence(m)
		return err
	case FireStopSound, JetpackStart, JetpackStop, RequestEnterVehicle:
		if m[1] >= 12 {
			return ErrPacket
		}
	case PlayerLoadout:
		_, err := ReadLoadout(m)
		return err
	case ChangeWeapon, FireStop:
		_, err := ReadWeaponAction(m)
		return err
	case AimWeapon:
		_, err := ReadAim(m)
		return err
	case ChangePlayerState:
		_, err := ReadPlayerState(m)
		return err
	case RespawnPlayer:
		_, err := ReadRespawn(m)
		return err
	case RoundSnapshot:
		_, err := ReadRoundClock(m)
		return err
	case RequestRespawn, ClientXP, ClientPurchasedInfo, StartPowerAnimation, DetonateMine, RequestEnterMachinegun, ExitMachinegun:
		if m[1] >= 12 {
			return ErrPacket
		}
	case LaunchBullet:
		_, err := ReadShot(m)
		return err
	case ThrowGrenade, ThrowGrenadeWithSpeed:
		_, err := ReadGrenadeThrow(m)
		return err
	case MeleeAttack:
		_, err := ReadMeleeRequest(m)
		return err
	case Discover:
		if binary.BigEndian.Uint32(m[1:]) != Compatibility {
			return ErrPacket
		}
	case ServerDetails, ClientDetails:
		n := 4
		if m[0] == ClientDetails {
			n = 16
		}
		if len(m) != 28+n || int(m[7]) != n || binary.BigEndian.Uint32(m[3:]) != Compatibility {
			return ErrPacket
		}
		if _, err := readName(m[8+n:]); err != nil {
			return err
		}
	case Roster:
		// Version, length-prefixed four-byte beacon, count, then fixed names,
		// length-prefixed sixteen-byte player details and transport slot IDs.
		if len(m) < 13 || m[7] != 4 || m[12] == 0 || m[12] > 12 || len(m) != 13+38*int(m[12]) {
			return ErrPacket
		}
		seen := uint32(0)
		for at := 13; at < len(m); at += 38 {
			if _, err := readName(m[at : at+20]); err != nil {
				return err
			}
			id := m[at+37]
			if m[at+20] != 16 || id >= 32 || seen&(1<<id) != 0 {
				return ErrPacket
			}
			seen |= 1 << id
		}
	case StartLoading:
		if m[1] >= 12 || m[12] < 1 || m[12] > 12 {
			return ErrPacket
		}
		for _, flag := range m[13:] {
			if flag > 1 {
				return ErrPacket
			}
		}
	case RequestClock, ClockReply:
		if m[1] >= 50 {
			return ErrPacket
		}
	}
	return nil
}

type Details struct {
	Name   string
	Beacon [4]byte
}
type Player struct {
	Name   string
	Beacon [16]byte
	Slot   byte
}

func Discovery() []byte {
	b := []byte{Discover, 0, 0, 0, 0}
	binary.BigEndian.PutUint32(b[1:], Compatibility)
	return b
}

func details(kind byte, name string, beacon []byte) ([]byte, error) {
	if !validName(name) {
		return nil, ErrPacket
	}
	b := make([]byte, 28+len(beacon))
	b[0] = kind
	binary.BigEndian.PutUint16(b[1:], uint16(len(b)))
	binary.BigEndian.PutUint32(b[3:], Compatibility)
	b[7] = byte(len(beacon))
	copy(b[8:], beacon)
	copy(b[8+len(beacon):], name)
	return b, nil
}
func (d Details) Message() ([]byte, error) { return details(ServerDetails, d.Name, d.Beacon[:]) }
func (p Player) Message() ([]byte, error)  { return details(ClientDetails, p.Name, p.Beacon[:]) }
func ReadPlayer(m []byte) (Player, error) {
	if len(m) != 44 || m[0] != ClientDetails || binary.BigEndian.Uint16(m[1:]) != 44 {
		return Player{}, ErrPacket
	}
	if err := validate(m); err != nil {
		return Player{}, err
	}
	name, _ := readName(m[24:])
	p := Player{Name: name}
	copy(p.Beacon[:], m[8:24])
	return p, nil
}

func PlayerList(version uint32, beacon [4]byte, players []Player) ([]byte, error) {
	if len(players) == 0 || len(players) > 12 {
		return nil, ErrPacket
	}
	b := make([]byte, 13+38*len(players))
	b[0] = Roster
	binary.BigEndian.PutUint16(b[1:], uint16(len(b)))
	binary.BigEndian.PutUint32(b[3:], version)
	b[7] = 4
	copy(b[8:], beacon[:])
	b[12] = byte(len(players))
	for i, p := range players {
		if !validName(p.Name) {
			return nil, ErrPacket
		}
		at := 13 + 38*i
		copy(b[at:at+20], p.Name)
		b[at+20] = 16
		copy(b[at+21:at+37], p.Beacon[:])
		b[at+37] = p.Slot
	}
	if err := validate(b); err != nil {
		return nil, err
	}
	return b, nil
}

// Loading describes the proved wire fields, not an operator settings format.
// Parameter1 is the time limit in minutes (zero means unlimited); Parameter2
// is the score limit. Original HandleIncomingMessages stores them at +3bec and
// +3bf0; MP_CheckEndGame multiplies +3bec by 60000 before comparing elapsed ms.
// Flags retain their original values; game-rule legality belongs to the owner.
type Loading struct {
	Player, Map            byte
	Parameter1, Parameter2 uint32
	Mode, Capacity         byte
	Flags                  [5]byte
}

func (v Loading) Message() ([]byte, error) {
	b := make([]byte, 18)
	b[0], b[1], b[2] = StartLoading, v.Player, v.Map
	binary.BigEndian.PutUint32(b[3:], v.Parameter1)
	binary.BigEndian.PutUint32(b[7:], v.Parameter2)
	b[11], b[12] = v.Mode, v.Capacity
	copy(b[13:], v.Flags[:])
	if err := validate(b); err != nil {
		return nil, err
	}
	return b, nil
}
