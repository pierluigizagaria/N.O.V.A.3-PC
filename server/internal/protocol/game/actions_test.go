package game

import (
	"bytes"
	"encoding/hex"
	"testing"
)

func TestOriginalActionMessages(t *testing.T) {
	// These goldens execute the complete native builders in the x86 probe.
	for _, row := range []struct {
		kind   byte
		value  int32
		golden string
	}{
		{ChangeWeapon, 0x01020304, "220b01020304"},
		{FireStop, -1, "370bffffffff"},
	} {
		a := WeaponAction{11, row.value}
		m, err := a.Message(row.kind)
		if err != nil || hex.EncodeToString(m) != row.golden {
			t.Fatal(m, err)
		}
		if got, err := ReadWeaponAction(m); err != nil || got != a {
			t.Fatal(got, err)
		}
	}
	for _, enabled := range []bool{false, true} {
		a := Aim{11, enabled}
		m, err := a.Message()
		want := "240b"
		if enabled {
			want = "248b"
		}
		if err != nil || hex.EncodeToString(m) != want {
			t.Fatal(m, err)
		}
		if got, err := ReadAim(m); err != nil || got != a {
			t.Fatal(got, err)
		}
	}
	s := PlayerState{11, ReloadState}
	m, err := s.Message()
	if err != nil || hex.EncodeToString(m) != "89b3" {
		t.Fatal(m, err)
	}
	if got, err := ReadPlayerState(m); err != nil || got != s {
		t.Fatal(got, err)
	}
	for _, golden := range []string{"220b01020304", "370bffffffff", "240b", "248b", "89b3", "210b01020304050607080901"} {
		m, _ := hex.DecodeString(golden)
		p, err := Encode(Header{Reliable: true}, m)
		if err != nil {
			t.Fatal(err)
		}
		got, err := Decode(p)
		if err != nil || len(got.Messages) != 1 || !bytes.Equal(got.Messages[0], m) {
			t.Fatal(got, err)
		}
		for n := HeaderSize; n < len(p); n++ {
			if _, err := Decode(p[:n]); err == nil {
				t.Fatal("truncated action", golden, n)
			}
		}
	}
}

func TestInvalidActions(t *testing.T) {
	for _, bad := range [][]byte{{ChangeWeapon, 12, 0, 0, 0, 0}, {FireStop, 255, 0, 0, 0, 0}, {AimWeapon, 12}, {AimWeapon, 0x8c}, {ChangePlayerState, 0xc3}} {
		if _, err := Encode(Header{}, bad); err == nil {
			t.Fatal("invalid actor accepted", bad)
		}
	}
	for _, a := range []WeaponAction{{12, 0}, {255, 0}} {
		if _, err := a.Message(ChangeWeapon); err == nil {
			t.Fatal("invalid weapon actor")
		}
	}
	if _, err := (WeaponAction{}).Message(0); err == nil {
		t.Fatal("invalid kind")
	}
	if _, err := (Aim{Actor: 12}).Message(); err == nil {
		t.Fatal("invalid aim actor")
	}
	for _, s := range []PlayerState{{12, 3}, {0, 16}} {
		if _, err := s.Message(); err == nil {
			t.Fatal("invalid state")
		}
	}
}
