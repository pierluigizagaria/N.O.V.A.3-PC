package game

import (
	"encoding/hex"
	"testing"
)

func TestOriginalLoadoutAndBeacon(t *testing.T) {
	v := Loadout{Actor: 11, Selections: [9]byte{1, 2, 3, 4, 5, 6, 7, 8, 9}, Jetpack: true}
	m, err := v.Message()
	if err != nil || hex.EncodeToString(m) != "210b01020304050607080901" {
		t.Fatal(m, err)
	}
	got, err := ReadLoadout(m)
	if err != nil || got != v {
		t.Fatal(got, err)
	}
	p := Player{Slot: 11, Beacon: [16]byte{0: 42, 3: 43, 14: 44, 15: 45}}
	p.SetLoadout(v)
	got, err = p.Loadout()
	if err != nil || got != v || p.Beacon[0] != 42 || p.Beacon[3] != 43 || p.Beacon[14] != 44 || p.Beacon[15] != 45 {
		t.Fatal(p, got, err)
	}
	for at, value := range map[int]byte{0: 34, 1: 12, 2: 128, 4: 255, 11: 2} {
		bad := append([]byte{}, m...)
		bad[at] = value
		if _, err := ReadLoadout(bad); err == nil {
			t.Fatal("invalid loadout", bad)
		}
		if _, err := Encode(Header{}, bad); err == nil {
			t.Fatal("invalid packet", bad)
		}
	}
}
