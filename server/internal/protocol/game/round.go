package game

import "encoding/binary"

const (
	RoundCountdown      byte = 132
	RoundStarted        byte = 133
	RoundWaiting        byte = 134
	RoundSnapshot       byte = 135
	ClientPurchasedInfo byte = 163 // Client claim, never an entitlement grant.
	ClientXP            byte = 164 // Untrusted original round-transition notification.
)

// RoundClock is the native post-powerup clock state. CountdownAt is in the
// synchronized match epoch; Elapsed is active round time, excluding waiting.
// Original fields: GameMpManager +afe0/+afdc/+afe8/+afe9 respectively.
type RoundClock struct {
	CountdownAt, Elapsed  uint32
	Running, CountingDown bool
}

func (r RoundClock) Message() ([]byte, error) {
	if r.Elapsed > 0x7fffffff || (r.Running && r.CountingDown) {
		return nil, ErrPacket
	}
	m := []byte{RoundSnapshot, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
	binary.BigEndian.PutUint32(m[1:], r.CountdownAt)
	binary.BigEndian.PutUint32(m[5:], r.Elapsed)
	if r.Running {
		m[9] = 1
	}
	if r.CountingDown {
		m[10] = 1
	}
	return m, nil
}

func ReadRoundClock(m []byte) (RoundClock, error) {
	if len(m) != 11 || m[0] != RoundSnapshot || m[9] > 1 || m[10] > 1 {
		return RoundClock{}, ErrPacket
	}
	r := RoundClock{binary.BigEndian.Uint32(m[1:]), binary.BigEndian.Uint32(m[5:]), m[9] == 1, m[10] == 1}
	if _, err := r.Message(); err != nil {
		return RoundClock{}, err
	}
	return r, nil
}

func RoundCountdownMessage(at uint32) []byte {
	return binary.BigEndian.AppendUint32([]byte{RoundCountdown}, at)
}
