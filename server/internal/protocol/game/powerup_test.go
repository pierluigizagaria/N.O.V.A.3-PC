package game

import (
	"bytes"
	"encoding/hex"
	"testing"
)

func TestOriginalPowerupNotificationGoldens(t *testing.T) {
	for _, wire := range []string{"25020102030403", "280102030401", "2701020304034d12345a56780064019003840102030500", "2701020304010012345a56780064019003840102030500"} {
		body, _ := hex.DecodeString(wire)
		var encoded []byte
		var err error
		switch body[0] {
		case ConsumePowerup:
			v, e := ReadPowerupConsumption(body)
			if e != nil || v.Actor != 2 || v.Object != 0x01020304 || v.Linked != 3 {
				t.Fatal(v, e)
			}
			encoded, err = v.Message()
		case MapPowerupSnapshot:
			v, e := ReadMapPowerupState(body)
			if e != nil || v.Object != 0x01020304 || !v.Visible {
				t.Fatal(v, e)
			}
			encoded = v.Message()
		case PowerupSnapshot:
			v, e := ReadPowerupState(body)
			if e != nil || v.Object != 0x01020304 || v.Weapon != 0x1234 || v.Ammo != 90 || v.Power != 0x5678 || v.Position != [3]uint16{100, 400, 900} || v.Zone != 0x01020305 || v.Linked != 0 {
				t.Fatal(v, e)
			}
			encoded, err = v.Message()
		}
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
		for _, invalid := range [][]byte{body[:len(body)-1], append(append([]byte{}, body...), 0)} {
			if _, err := Encode(Header{}, invalid); err == nil {
				t.Fatal("invalid length", invalid)
			}
		}
	}
}

func TestPowerupStateRejectsInvalidFlagsAndActors(t *testing.T) {
	for _, body := range [][]byte{{37, 12, 0, 0, 0, 0, 0}, {40, 0, 0, 0, 0, 2}} {
		if _, err := Encode(Header{}, body); err == nil {
			t.Fatal("invalid powerup", body)
		}
	}
	for _, state := range []PowerupState{{Flags: 4}, {State: 1}, {Flags: 1, State: 1}} {
		if _, err := state.Message(); err == nil {
			t.Fatal("invalid pickup state", state)
		}
	}
}
