package game

import (
	"bytes"
	"encoding/hex"
	"testing"
)

func TestOriginalCombatNotificationGoldens(t *testing.T) {
	// Shared with execution of the original serializers/receiver. Not proof
	// of complete hit detection, death rules or scoring in the simulation.
	for _, tc := range []struct {
		wire  string
		value float32
	}{
		{"15000107f02021", 7.5},
		{"15000107002021", 0}, // Native <=8 branch wraps exactly8*32 to byte0.
		{"15000106192021", 25},
		{"15000106ff2021", 255},
	} {
		body, _ := hex.DecodeString(tc.wire)
		d, err := ReadDamage(body)
		if err != nil || d.Value() != tc.value || d.Attacker != 0 || d.Victim != 1 || d.Kind != 2 || d.Direction != 0 || d.HitEffect != 0x21 {
			t.Fatal(d, err)
		}
		encoded, err := d.Message()
		if err != nil || !bytes.Equal(encoded, body) {
			t.Fatal(encoded, err)
		}
		packet, err := Encode(Header{}, body)
		if err != nil {
			t.Fatal(err)
		}
		if _, err = Decode(packet); err != nil {
			t.Fatal(err)
		}
	}
	for _, wire := range []string{"16000103", "1700011903"} {
		body, _ := hex.DecodeString(wire)
		h, err := ReadHit(body)
		if err != nil || h.Attacker != 0 || h.Victim != 1 || h.Part != 3 {
			t.Fatal(h, err)
		}
		encoded, err := h.Message()
		if err != nil || !bytes.Equal(encoded, body) {
			t.Fatal(encoded, err)
		}
	}
	for flags := byte(0); flags < 16; flags++ {
		body := []byte{29, 1, 0, 0x12, 0x34, flags, 5, 0x30}
		d, err := ReadDeath(body)
		if err != nil || d.Victim != 1 || d.Attacker != 0 || d.Weapon != 0x1234 || d.Flags != flags || d.Source != 5 || d.Part != 3 || d.Direction != 0 {
			t.Fatal(d, err)
		}
		encoded, err := d.Message()
		if err != nil || !bytes.Equal(encoded, body) {
			t.Fatal(encoded, err)
		}
	}
}

func TestCombatNotificationsRejectNoncanonicalFields(t *testing.T) {
	for _, body := range [][]byte{{21, 0, 1, 8, 1, 0, 0}, {29, 1, 0, 0, 0, 16, 0, 0}, {21, 0}, {22, 0, 0}, {23, 0, 0, 0}, {29, 0}} {
		if _, err := Encode(Header{}, body); err == nil {
			t.Fatal("accepted invalid combat notification", body)
		}
	}
	if _, err := (Damage{Direction: 16}).Message(); err == nil {
		t.Fatal("direction overflow")
	}
	if _, err := (Damage{Kind: 16}).Message(); err == nil {
		t.Fatal("kind overflow")
	}
	if _, err := (Hit{Amount: 1}).Message(); err == nil {
		t.Fatal("player observer cannot carry amount")
	}
	if _, err := (Death{Part: 16}).Message(); err == nil {
		t.Fatal("part overflow")
	}
	if _, err := (Death{Direction: 16}).Message(); err == nil {
		t.Fatal("death direction overflow")
	}
}
