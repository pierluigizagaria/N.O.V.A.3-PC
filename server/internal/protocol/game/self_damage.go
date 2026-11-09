package game

import (
	"encoding/binary"
	"math"
)

const RequestSelfDamage byte = 126

// SelfDamage is a client intent, not an authoritative damage amount. Original
// clients send it for local environmental effects; the server owns the result.
type SelfDamage struct {
	Actor  byte
	Amount float32
}

func ReadSelfDamage(m []byte) (SelfDamage, error) {
	if len(m) != 6 || m[0] != RequestSelfDamage || m[1] >= 12 {
		return SelfDamage{}, ErrPacket
	}
	v := SelfDamage{m[1], math.Float32frombits(binary.BigEndian.Uint32(m[2:]))}
	if math.IsNaN(float64(v.Amount)) || math.IsInf(float64(v.Amount), 0) || v.Amount < 0 {
		return SelfDamage{}, ErrPacket
	}
	return v, nil
}
