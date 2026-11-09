package game

import (
	"bytes"
	"encoding/hex"
	"testing"
)

func TestNativeRoundClock(t *testing.T) {
	r := RoundClock{CountdownAt: 0x01020304, Elapsed: 0x11223344, Running: true}
	m, err := r.Message()
	if err != nil || hex.EncodeToString(m) != "8701020304112233440100" {
		t.Fatal(m, err)
	}
	back, err := ReadRoundClock(m)
	if err != nil || back != r {
		t.Fatal(back, err)
	}
	for _, body := range [][]byte{m[:10], append(bytes.Clone(m), 0), {135, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1}, {135, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2}} {
		if _, err := ReadRoundClock(body); err == nil {
			t.Fatal("invalid clock", body)
		}
	}
	if hex.EncodeToString(RoundCountdownMessage(0x01020304)) != "8401020304" {
		t.Fatal("countdown encoding")
	}
	for _, body := range [][]byte{m, RoundCountdownMessage(123), {RoundStarted}, {RoundWaiting}} {
		packet, err := Encode(Header{}, body)
		if err != nil {
			t.Fatal(err)
		}
		if p, err := Decode(packet); err != nil || len(p.Messages) != 1 {
			t.Fatal(p, err)
		}
	}
}
