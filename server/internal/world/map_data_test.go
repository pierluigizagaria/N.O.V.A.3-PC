package world

import (
	"bytes"
	"encoding/binary"
	"math"
	"os"
	"testing"
)

func mapFixture() []byte {
	var b bytes.Buffer
	b.WriteString("NV3C\x03")
	write := func(v any) { _ = binary.Write(&b, binary.BigEndian, v) }
	write(uint8(2))
	write(uint16(246))
	write(uint32(1))
	write(uint32(2))
	write(uint32(11))
	write(uint32(5004))
	write(uint8(3))
	write([9]float32{0, 0, 2, 0, 0, 0, 1, 1, 1})
	write(uint32(2))
	for _, material := range []uint8{0, 18} {
		write([9]float32{-100, -100, 0, 100, -100, 0, 0, 100, 0})
		write(material)
	}
	for i := uint32(1); i <= 2; i++ {
		write(i)
		write([9]float32{float32(i), 0, 4, 0, 0, 90, 1, 1, 1})
		write(uint8(1))
		write([7]int32{int32(i), -1, -1, -1, -1, -1, -1})
	}
	write(uint32(2))
	write([2]uint32{123, 456})
	write([6]float32{-100, -100, -100, 100, 100, 100})
	return b.Bytes()
}

func TestCollisionMapToScene(t *testing.T) {
	m, err := DecodeCollisionMap(bytes.NewReader(mapFixture()))
	if err != nil {
		t.Fatal(err)
	}
	if m.ID != 2 || m.ModeBits != 246 || len(m.Objects) != 1 || len(m.Spawns) != 2 || len(m.Objects[0].Parts) != 2 {
		t.Fatalf("map identity/parts: %+v", m)
	}
	if m.Objects[0].Type != 5004 || !m.Objects[0].Static || m.Spawns[1].Team != 2 || m.Spawns[0].LinkedSpawn != -1 {
		t.Fatal("lost native metadata")
	}
	s, err := m.NewScene()
	if err != nil {
		t.Fatal(err)
	}
	h, hit, err := s.Raycast(Vec3{0, 0, 4}, Vec3{}, Filter{1, 2}, nil)
	assertHit(t, h, hit, err, .5, 11)
	if h.Material != 0 {
		t.Fatal("material group ignored")
	}
	h, hit, err = s.Raycast(Vec3{0, 0, 4}, Vec3{}, Filter{1, 0x800}, nil)
	assertHit(t, h, hit, err, .5, 11)
	if h.Material != 18 {
		t.Fatal("invisible collision excluded")
	}
	for mode, bit := range []uint16{2, 4, 8, 32, 64, 16, 128, 1} {
		for test := uint16(1); test <= 128; test <<= 1 {
			m.ModeBits = test
			if m.Supports(uint8(mode)) != (test == bit) {
				t.Fatal("mode ID confused with bit index", mode, test)
			}
		}
	}
	if m.Supports(8) || m.Supports(255) {
		t.Fatal("unsupported native mode")
	}
}

func TestCollisionMapRejectsPartialOrCorruptSections(t *testing.T) {
	fixture := mapFixture()
	for cut := 0; cut < len(fixture); cut++ {
		if m, err := DecodeCollisionMap(bytes.NewReader(fixture[:cut])); err == nil || m != nil {
			t.Fatal("accepted truncation", cut)
		}
	}
	for _, mutate := range []func([]byte){
		func(b []byte) { b[4] = 4 },
		func(b []byte) { b[6], b[7] = 0, 0 },
		func(b []byte) { binary.BigEndian.PutUint32(b[8:12], 0xffffffff) },
		func(b []byte) { binary.BigEndian.PutUint32(b[12:16], 0) },
		func(b []byte) { b[24] = 128 },
		func(b []byte) { binary.BigEndian.PutUint32(b[25:29], math.Float32bits(float32(math.Inf(1)))) },
		func(b []byte) { binary.BigEndian.PutUint32(b[61:65], 500001) },
		func(b []byte) { binary.BigEndian.PutUint32(b[139:143], 11) },     // Spawn duplicates collider.
		func(b []byte) { binary.BigEndian.PutUint32(b[len(b)-40:], 999) }, // Missing linked spawn.
		func(b []byte) { binary.BigEndian.PutUint32(b[len(b)-36:], 4097) },
		func(b []byte) { binary.BigEndian.PutUint32(b[len(b)-28:], 123) }, // Duplicate zone.
		func(b []byte) { binary.BigEndian.PutUint32(b[len(b)-28:], 0xffffffff) },
		func(b []byte) { binary.BigEndian.PutUint32(b[len(b)-24:], math.Float32bits(float32(math.NaN()))) },
		func(b []byte) { binary.BigEndian.PutUint32(b[len(b)-24:], math.Float32bits(100)) }, // Equal bounds.
		func(b []byte) { binary.BigEndian.PutUint32(b[len(b)-4:], math.Float32bits(-200)) }, // Inverted Z.
	} {
		data := bytes.Clone(fixture)
		mutate(data)
		if m, err := DecodeCollisionMap(bytes.NewReader(data)); err == nil || m != nil {
			t.Fatal("accepted corrupt section")
		}
	}
	if m, err := DecodeCollisionMap(bytes.NewReader(append(fixture, 0))); err == nil || m != nil {
		t.Fatal("accepted trailing data")
	}
}

// The development compiler can stream a section to this opt-in check without
// retaining fixtures or adding a server operator import command.
func TestCompiledCollisionMap(t *testing.T) {
	if os.Getenv("NOVA_MAP_STDIN") != "1" {
		t.Skip("development compiler integration is opt-in")
	}
	m, err := DecodeCollisionMap(os.Stdin)
	if err != nil {
		t.Fatal(err)
	}
	scene, err := m.NewScene()
	if err != nil || scene == nil {
		t.Fatal("compiled map geometry", err)
	}
	t.Logf("map=%d objects=%d spawns=%d", m.ID, len(m.Objects), len(m.Spawns))
}
