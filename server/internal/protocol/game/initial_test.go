package game

import (
	"bytes"
	"encoding/hex"
	"testing"
)

func TestOriginalInitialSnapshotLayout(t *testing.T) {
	s := InitialSnapshot{RenderDelayMS: 0x11223344}
	for i := 0; i < 12; i++ {
		p := InitialPlayer{Player: Player{Slot: byte(i), Name: "R06Peer"}, Active: i < 2,
			Kills: 1, Deaths: 2, Flag: 3, Score: 0x01020304, AuxiliaryCounters: [3]uint16{4, 5, 6}, Dead: true, HasPowerup: true, Weapon: 23085}
		for j := range p.Player.Beacon {
			p.Player.Beacon[j] = byte(0x10 + j)
		}
		s.Players = append(s.Players, p)
	}
	b, err := s.Message()
	if err != nil || len(b) != 781 {
		t.Fatal(len(b), err)
	}
	if b[0] != 56 || !bytes.Equal(b[1:21], make([]byte, 20)) || hex.EncodeToString(b[21:25]) != "11223344" {
		t.Fatal("snapshot prefix")
	}
	// Same fabricated records are serialized by the original x86 builder.
	for i := 0; i < 12; i++ {
		r := b[25+i*63 : 25+(i+1)*63]
		if r[0] != 1 || r[2] != byte(i) || r[3] != byte(i) || (r[1] == 1) != (i < 2) {
			t.Fatal("identity", i)
		}
		if !bytes.Equal(r[4:20], s.Players[i].Player.Beacon[:]) || string(r[20:27]) != "R06Peer" {
			t.Fatal("beacon/name", i)
		}
		if hex.EncodeToString(r[40:]) != "00010002000301020304000400050006035a2d00000000" {
			t.Fatal("snapshot tail", i)
		}
	}
	if _, err := Encode(Header{}, b); err != nil {
		t.Fatal(err)
	}
	empty, err := (InitialSnapshot{}).Message()
	if err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 12; i++ {
		r := empty[25+i*63:]
		if r[0] != 0 || r[1] != 0 || r[2] != 255 || r[3] != 255 {
			t.Fatal("unused actor", i)
		}
	}
	s.Players[11].Player.Slot = 0
	if _, err = s.Message(); err == nil {
		t.Fatal("duplicate slot")
	}
}
