package game

import (
	"bytes"
	"encoding/hex"
	"testing"
)

func TestOriginalPostLoadingGoldens(t *testing.T) {
	reply, _ := ClockSample(49, 0xfffffff0)
	for _, v := range []struct {
		body   []byte
		golden string
	}{
		{[]byte{RequestClock, 49}, "0631"},
		{reply, "0731fffffff0"},
		{ClockCorrection(2, -1000), "0800000002fffffc18"},
		{[]byte{Ping, 1, 2, 3, 4, 0, 0, 0, 0}, "0d0102030400000000"},
		{(Start{RemainingMS: 2000, ElapsedMS: 0x11223344, Seed: 0x12345678, Spawn: 0x01020304}).Message(), "12000007d0112233441234567801020304"},
	} {
		want, _ := hex.DecodeString(v.golden)
		if !bytes.Equal(v.body, want) {
			t.Fatal("native golden", v.body, want)
		}
		wire, err := Encode(Header{}, v.body)
		if err != nil {
			t.Fatal(err)
		}
		for n := HeaderSize; n < len(wire); n++ {
			if _, err := Decode(wire[:n]); err == nil {
				t.Fatal("truncated message", n)
			}
		}
		packet, err := Decode(wire)
		if err != nil || !bytes.Equal(packet.Messages[0], want) {
			t.Fatal(err)
		}
	}
	if _, err := ClockSample(50, 0); err == nil {
		t.Fatal("native sample array overflow")
	}
	if _, err := Encode(Header{}, []byte{RequestClock, 50}); err == nil {
		t.Fatal("invalid sample")
	}
}
