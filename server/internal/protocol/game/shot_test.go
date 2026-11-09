package game

import (
	"bytes"
	"encoding/hex"
	"testing"
)

func TestOriginalShotGoldens(t *testing.T) {
	base := Shot{Time: 0x01020304, Actor: 2, Origin: [3]uint16{100, 400, 900}, Direction: [3]uint16{65535, 32767, 32767}, Weapon: 1, SourceType: 3}
	full := base
	full.Flags = 31
	full.Scalar = 9830
	full.SourceType = 1
	full.SourceIndex = 7
	for _, test := range []struct {
		value Shot
		hex   string
	}{
		{base, "1800160102030402006401900384ffff7fff7fff0100"},
		{full, "18001a0102030402006401900384ffff7fff7fff011f26660107"},
	} {
		golden, err := hex.DecodeString(test.hex)
		if err != nil {
			t.Fatal(err)
		}
		encoded, err := test.value.Message()
		if err != nil || !bytes.Equal(encoded, golden) {
			t.Fatalf("shot golden: %x %v", encoded, err)
		}
		decoded, err := ReadShot(golden)
		if err != nil || decoded != test.value {
			t.Fatalf("shot decode: %+v %v", decoded, err)
		}
		packet, err := Encode(Header{}, golden, []byte{Acknowledge})
		if err != nil {
			t.Fatal(err)
		}
		p, err := Decode(packet)
		if err != nil || len(p.Messages) != 2 {
			t.Fatal("shot consumed following record", err)
		}
		for n := 0; n < len(golden); n++ {
			if _, err := ReadShot(golden[:n]); err == nil {
				t.Fatal("accepted truncation", n)
			}
		}
		if _, err := ReadShot(append(bytes.Clone(golden), 0)); err == nil {
			t.Fatal("accepted trailing data")
		}
	}
}

func TestShotFlagsAndOptionalFieldBoundaries(t *testing.T) {
	for flags := 0; flags < 256; flags++ {
		v := Shot{Flags: byte(flags), SourceType: 3}
		if flags&2 != 0 {
			v.Scalar = 0x1234
		}
		if flags&8 != 0 {
			v.SourceType = 1
			v.SourceIndex = 255
		}
		b, err := v.Message()
		if flags > 31 {
			if err == nil {
				t.Fatal("unknown flags accepted", flags)
			}
			continue
		}
		if err != nil {
			t.Fatal(err)
		}
		decoded, err := ReadShot(b)
		if err != nil || decoded != v {
			t.Fatal("optional field mismatch", flags, err)
		}
		b[2]--
		if _, err := ReadShot(b); err == nil {
			t.Fatal("size mismatch accepted")
		}
	}
	for _, v := range []Shot{{SourceType: 3, Scalar: 1}, {SourceType: 3, SourceIndex: 1}, {SourceType: 3, Flags: 8}, {SourceType: 1}} {
		if _, err := v.Message(); err == nil {
			t.Fatal("noncanonical optional fields accepted", v)
		}
	}
}
