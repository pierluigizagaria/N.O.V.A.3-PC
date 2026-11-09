package game

import "encoding/binary"

// ClockSample uses the client's original OS clock, not wall-clock/Unix time.
func ClockSample(sample byte, clock uint32) ([]byte, error) {
	if sample >= 50 {
		return nil, ErrPacket
	}
	b := []byte{ClockReply, sample, 0, 0, 0, 0}
	binary.BigEndian.PutUint32(b[2:], clock)
	return b, nil
}

// ClockCorrection makes the client store -offset as its clock epoch. The
// original midpoint estimate is (serverSend+serverReceive)/2 - clientClock.
func ClockCorrection(rtt uint32, offset int32) []byte {
	b := make([]byte, 9)
	b[0] = SetClock
	binary.BigEndian.PutUint32(b[1:], rtt)
	binary.BigEndian.PutUint32(b[5:], uint32(offset))
	return b
}

// Start is the original type18 wire record. Spawn is an actual map spawn-object
// ID, not a player slot or a made-up coordinate. Encoding it does not create a
// world or authorize gameplay; a scene owner must supply the verified values.
type Start struct {
	RemainingMS            int32
	ElapsedMS, Seed, Spawn uint32
}

func (v Start) Message() []byte {
	b := make([]byte, 17)
	b[0] = StartGame
	for i, n := range []uint32{uint32(v.RemainingMS), v.ElapsedMS, v.Seed, v.Spawn} {
		binary.BigEndian.PutUint32(b[1+4*i:], n)
	}
	return b
}
