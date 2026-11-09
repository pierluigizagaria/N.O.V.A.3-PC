package game

import (
	"bytes"
	"encoding/hex"
	"testing"
)

func TestOriginalDiscoveryAndLoadingGoldens(t *testing.T) {
	server, _ := (Details{"R06Host", [4]byte{0, 1, 2, 0}}).Message()
	p := Player{Name: "R06Peer", Beacon: [16]byte{0, 0x11, 0x22, 0x33}, Slot: 1}
	client, _ := p.Message()
	roster, _ := PlayerList(1, [4]byte{0, 1, 2, 0}, []Player{{Name: "R06Host"}, p})
	loading, _ := (Loading{Player: 1, Parameter1: 300, Parameter2: 600, Capacity: 2, Flags: [5]byte{1, 0, 1, 0, 1}}).Message()
	cases := []struct {
		h    Header
		body []byte
		hex  string
	}{
		{Header{}, Discovery(), "00000000000000000000000000010ab25828"},
		{Header{}, server, "000000000000000000000000000200200ab258280400010200523036486f737400000000000000000000000000"},
		{Header{Reliable: true}, client, "0100000000000000000000000003002c0ab2582810001122330000000000000000000000005230365065657200000000000000000000000000"},
		{Header{Reliable: true, AckMask: 1}, roster, "0100000000000000000100000005005900000001040001020002523036486f7374000000000000000000000000001000000000000000000000000000000000005230365065657200000000000000000000000000100011223300000000000000000000000001"},
		{Header{Reliable: true, Sequence: 1, AckMask: 1}, loading, "010100000000000000010000001001000000012c0000025800020100010001"},
	}
	for _, v := range cases {
		want, _ := hex.DecodeString(v.hex)
		got, err := Encode(v.h, v.body)
		if err != nil || !bytes.Equal(got, want) {
			t.Fatalf("type=%d got=%x err=%v", v.body[0], got, err)
		}
		decoded, err := Decode(want)
		if err != nil || decoded.Header != v.h || len(decoded.Messages) != 1 || !bytes.Equal(decoded.Messages[0], v.body) {
			t.Fatal("golden decode", err)
		}
	}
}

func TestGameDatagramAdmissionBounds(t *testing.T) {
	d, _ := (Player{Name: "Peer"}).Message()
	wire, _ := Encode(Header{Reliable: true}, d)
	for i := 0; i < len(wire); i++ {
		if _, err := Decode(wire[:i]); err == nil {
			t.Fatalf("accepted truncation %d", i)
		}
	}
	for _, mutate := range []func([]byte){
		func(b []byte) { b[0] = 2 }, func(b []byte) { b[13] = 255 }, func(b []byte) { b[14] = 0xff },
		func(b []byte) { b[16] ^= 1 }, func(b []byte) { b[20] = 15 },
		func(b []byte) {
			for i := 37; i < len(b); i++ {
				b[i] = 'x'
			}
		},
	} {
		b := bytes.Clone(wire)
		mutate(b)
		if _, err := Decode(b); err == nil {
			t.Fatalf("accepted malformed %x", b)
		}
	}
	if _, err := Decode(append(bytes.Clone(wire), 0xff)); err == nil {
		t.Fatal("trailing unknown message")
	}
	if _, err := Encode(Header{}, make([]byte, MaxDatagram)); err == nil {
		t.Fatal("oversized")
	}
	if _, err := Encode(Header{}, bytes.Repeat([]byte{Acknowledge}, 65)); err == nil {
		t.Fatal("message count")
	}
	for _, name := range []string{"", "12345678901234567890", "a\x00b", "a\nb"} {
		if _, err := (Player{Name: name}).Message(); err == nil {
			t.Fatal("invalid native name", name)
		}
	}
	b, _ := Encode(Header{}, []byte{Acknowledge}, []byte{FinishedLoading})
	p, err := Decode(b)
	if err != nil || len(p.Messages) != 2 {
		t.Fatal("coalesced fixed messages", err)
	}
	b[13] = 0
	if p.Messages[0][0] != Acknowledge {
		t.Fatal("decoded storage aliases input")
	}
}

func TestRosterAndLoadingRejectUnsafeState(t *testing.T) {
	if _, err := PlayerList(1, [4]byte{}, []Player{{Name: "A", Slot: 2}, {Name: "B", Slot: 2}}); err == nil {
		t.Fatal("duplicate slot")
	}
	for _, v := range []Loading{{Capacity: 0}, {Capacity: 13}, {Player: 12, Capacity: 12}, {Capacity: 2, Flags: [5]byte{2}}} {
		if _, err := v.Message(); err == nil {
			t.Fatal("unsafe loading", v)
		}
	}
}

func FuzzDecode(f *testing.F) {
	b, _ := Encode(Header{}, Discovery())
	f.Add(b)
	p, _ := (Player{Name: "Peer"}).Message()
	b, _ = Encode(Header{Reliable: true}, p)
	f.Add(b)
	f.Fuzz(func(t *testing.T, b []byte) {
		p, err := Decode(b)
		if err != nil {
			return
		}
		again, err := Encode(p.Header, p.Messages...)
		if err != nil || !bytes.Equal(again, b) {
			t.Fatal("valid packet did not roundtrip")
		}
	})
}
