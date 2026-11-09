package game

import (
	"bytes"
	"encoding/binary"
	"encoding/hex"
	"math"
	"testing"
)

func TestOriginalGrenadeAndMeleeGoldens(t *testing.T) {
	v := GrenadeThrow{Actor: 2, Time: 0x01020304, Origin: [3]uint16{100, 400, 900}, Direction: [3]uint16{65535, 32767, 32767}, Movement: [3]uint16{32767, 65535, 32767}, Grenade: 1, Weapon: 1}
	for _, speed := range []float32{0, 1.5} {
		v.Speed = speed
		golden := "1a0201020304006401900384ffff7fff7fff7fffffff7fff0101"
		if speed != 0 {
			golden = "1b" + golden[2:] + "0000c03f"
		}
		b, err := v.Message()
		if err != nil || hex.EncodeToString(b) != golden {
			t.Fatal(b, err)
		}
		got, err := ReadGrenadeThrow(b)
		if err != nil || got != v {
			t.Fatal(got, err)
		}
		for n := range len(b) {
			if _, err := ReadGrenadeThrow(b[:n]); err == nil {
				t.Fatal("truncated grenade", n)
			}
		}
		packet, err := Encode(Header{Reliable: true}, b, []byte{Acknowledge})
		if err != nil {
			t.Fatal(err)
		}
		decoded, err := Decode(packet)
		if err != nil || len(decoded.Messages) != 2 || !bytes.Equal(decoded.Messages[0], b) {
			t.Fatal(decoded, err)
		}
	}
	m := MeleeRequest{Time: 0x01020304, Victim: 1, Origin: [3]uint16{100, 400, 900}, Weapon: 1}
	b, err := m.Message()
	if err != nil || hex.EncodeToString(b) != "3e010203040100640190038401" {
		t.Fatal(b, err)
	}
	got, err := ReadMeleeRequest(b)
	if err != nil || got != m {
		t.Fatal(got, err)
	}
	for n := range len(b) {
		if _, err := ReadMeleeRequest(b[:n]); err == nil {
			t.Fatal("truncated melee", n)
		}
	}
}

func TestGrenadeRejectsNoncanonicalSpeedAndInvalidActors(t *testing.T) {
	for _, speed := range []float32{-1, float32(math.NaN()), float32(math.Inf(1))} {
		if _, err := (GrenadeThrow{Speed: speed}).Message(); err == nil {
			t.Fatal("invalid speed", speed)
		}
	}
	b, _ := (GrenadeThrow{Speed: 1}).Message()
	for _, bits := range []uint32{0, 0x80000000, 0xbf800000, 0x7f800000, 0x7fc00000} {
		binary.LittleEndian.PutUint32(b[26:], bits)
		if _, err := ReadGrenadeThrow(b); err == nil {
			t.Fatal("invalid float bits", bits)
		}
	}
	if _, err := (GrenadeThrow{Actor: 12}).Message(); err == nil {
		t.Fatal("invalid grenade actor")
	}
	if _, err := (MeleeRequest{Victim: 12}).Message(); err == nil {
		t.Fatal("invalid melee victim")
	}
}
