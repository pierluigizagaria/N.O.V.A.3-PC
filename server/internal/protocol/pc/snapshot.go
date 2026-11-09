// Package pc defines the shared, versioned PC protocol for LAN and online.
// Authentication and authoritative world validation belong to its callers.
package pc

import (
	"encoding/binary"
	"errors"
	"math"
)

const (
	Version                    = 1
	HeaderSize                 = 8
	RecordSize                 = 29
	MaxPlayers                 = 12
	MaxPayload                 = 2048
	Native              byte   = 1
	Snapshots           byte   = 2
	Capabilities        byte   = 3
	Lives               byte   = 4
	SnapshotV1          uint32 = 1
	PresentationDelayMS uint32 = 67
)

var ErrPacket = errors.New("invalid PC protocol packet")

type Snapshot struct {
	Slot                       byte
	Generation, Sequence, Time uint32
	Position                   [3]float32
	Yaw, Pitch                 uint16
}

// Newer excludes equality and the ambiguous exactly-half-range distance.
func Newer(value, previous uint32) bool {
	difference := value - previous
	return difference != 0 && difference < 1<<31
}

func EncodeAngle(degrees float64) (uint16, error) {
	if math.IsNaN(degrees) || math.IsInf(degrees, 0) {
		return 0, ErrPacket
	}
	wrapped := math.Mod(degrees, 360)
	if wrapped < 0 {
		wrapped += 360
	}
	return uint16(uint32(math.Floor(wrapped*(65536.0/360)+0.5)) & 65535), nil
}

func DecodeAngle(value uint16) float64 { return float64(value) * (360.0 / 65536) }

func (s Snapshot) Valid() bool {
	if s.Slot >= MaxPlayers || s.Generation == 0 {
		return false
	}
	for _, x := range s.Position {
		if math.IsNaN(float64(x)) || math.IsInf(float64(x), 0) {
			return false
		}
	}
	return true
}

func EncodeSnapshots(records []Snapshot) ([]byte, error) {
	if len(records) == 0 || len(records) > MaxPlayers {
		return nil, ErrPacket
	}
	body := make([]byte, 1+len(records)*RecordSize)
	body[0] = byte(len(records))
	var slots uint16
	for i, s := range records {
		if !s.Valid() || slots&(1<<s.Slot) != 0 {
			return nil, ErrPacket
		}
		slots |= 1 << s.Slot
		b := body[1+i*RecordSize:]
		b[0] = s.Slot
		binary.BigEndian.PutUint32(b[1:], s.Generation)
		binary.BigEndian.PutUint32(b[5:], s.Sequence)
		binary.BigEndian.PutUint32(b[9:], s.Time)
		for axis, x := range s.Position {
			binary.BigEndian.PutUint32(b[13+axis*4:], math.Float32bits(x))
		}
		binary.BigEndian.PutUint16(b[25:], s.Yaw)
		binary.BigEndian.PutUint16(b[27:], s.Pitch)
	}
	return body, nil
}

func DecodeSnapshots(body []byte) ([]Snapshot, error) {
	if len(body) < 1 || body[0] == 0 || body[0] > MaxPlayers || len(body) != 1+int(body[0])*RecordSize {
		return nil, ErrPacket
	}
	records := make([]Snapshot, int(body[0]))
	var slots uint16
	for i := range records {
		b := body[1+i*RecordSize:]
		s := Snapshot{Slot: b[0], Generation: binary.BigEndian.Uint32(b[1:]), Sequence: binary.BigEndian.Uint32(b[5:]), Time: binary.BigEndian.Uint32(b[9:]), Yaw: binary.BigEndian.Uint16(b[25:]), Pitch: binary.BigEndian.Uint16(b[27:])}
		for axis := range s.Position {
			s.Position[axis] = math.Float32frombits(binary.BigEndian.Uint32(b[13+axis*4:]))
		}
		if !s.Valid() || slots&(1<<s.Slot) != 0 {
			return nil, ErrPacket
		}
		slots |= 1 << s.Slot
		records[i] = s
	}
	return records, nil
}

func validPayload(kind byte, body []byte) bool {
	switch kind {
	case Native:
		return len(body) > 0 && len(body) <= MaxPayload
	case Snapshots:
		_, err := DecodeSnapshots(body)
		return err == nil
	case Capabilities:
		return len(body) == 4 && binary.BigEndian.Uint32(body) == SnapshotV1
	case Lives:
		_, err := DecodeLives(body)
		return err == nil
	default:
		return false
	}
}

func Encode(kind byte, body []byte) ([]byte, error) {
	if !validPayload(kind, body) {
		return nil, ErrPacket
	}
	b := make([]byte, HeaderSize+len(body))
	copy(b, "N3PC")
	b[4], b[5] = Version, kind
	binary.BigEndian.PutUint16(b[6:], uint16(len(body)))
	copy(b[HeaderSize:], body)
	return b, nil
}

// Decode returns a view into data, valid only as long as the caller owns data.
func Decode(data []byte) (byte, []byte, error) {
	if len(data) < HeaderSize || string(data[:4]) != "N3PC" || data[4] != Version || int(binary.BigEndian.Uint16(data[6:])) != len(data)-HeaderSize {
		return 0, nil, ErrPacket
	}
	if !validPayload(data[5], data[HeaderSize:]) {
		return 0, nil, ErrPacket
	}
	return data[5], data[HeaderSize:], nil
}
