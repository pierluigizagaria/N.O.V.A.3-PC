package game

import (
	"encoding/hex"
	"testing"
)

func TestOriginalPresenceBuilder(t *testing.T) {
	p := Presence{Player: Player{Slot: 1, Name: "R06Peer"}, Spawn: 0x01020304}
	for i := range p.Player.Beacon {
		p.Player.Beacon[i] = byte(0x10 + i)
	}
	b, err := p.Message()
	if err != nil || hex.EncodeToString(b) != "13010101101112131415161718191a1b1c1d1e1f523036506565720000000000000000000000000001020304" {
		t.Fatal(b, err)
	}
	got, err := ReadPresence(b)
	if err != nil || got != p {
		t.Fatal(got, err)
	}
	for _, bad := range []Presence{{Player: Player{Slot: 12, Name: "a"}}, {Player: Player{Name: "a"}, Spawn: 0xffffffff}, {Player: Player{Name: ""}}} {
		if _, err := bad.Message(); err == nil {
			t.Fatal("invalid presence", bad)
		}
	}
}

func TestCompleteNativeFramingAndMixedMessages(t *testing.T) {
	if len(recordSizes) != 165 {
		t.Fatal("incomplete native framing table")
	}
	for kind := 1; kind < 165; kind++ {
		n := RecordSize(byte(kind))
		if n < 1 {
			t.Fatal(kind, n)
		}
		// Unimplemented semantic messages still have exact framing; don't test
		// known semantic validators with deliberately empty values.
		if kind == 64 || kind == 66 || kind == 91 || kind == 117 || kind == 131 {
			if n == variableRecord {
				n = 3
			}
			body := make([]byte, n)
			body[0] = byte(kind)
			if RecordSize(byte(kind)) == variableRecord {
				body[2] = 3
			}
			wire, err := Encode(Header{}, body, []byte{SceneReady})
			if err != nil {
				t.Fatal(kind, err)
			}
			p, err := Decode(wire)
			if err != nil || len(p.Messages) != 2 || p.Messages[1][0] != SceneReady {
				t.Fatal(kind, p, err)
			}
			if _, err := Decode(wire[:len(wire)-2]); err == nil {
				t.Fatal("truncated record accepted", kind)
			}
		}
	}
	for _, kind := range []byte{0, 165, 255} {
		if RecordSize(kind) != 0 {
			t.Fatal(kind)
		}
	}
}
