package world

import (
	"bytes"
	"compress/gzip"
	"encoding/binary"
	"testing"
)

func zipFixture(t *testing.T, body []byte) []byte {
	t.Helper()
	var output bytes.Buffer
	writer := gzip.NewWriter(&output)
	if _, err := writer.Write(body); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	return output.Bytes()
}

func TestMapBundleRejectsCorruption(t *testing.T) {
	section := mapFixture()
	section[5] = 0
	body := append([]byte("NV3W\x01\x01"), 0, 0, 0, 0)
	binary.BigEndian.PutUint32(body[6:], uint32(len(section)))
	body = append(body, section...)
	compressed := zipFixture(t, body)
	if sections, err := decodeMapBundle(compressed); err != nil || len(sections) != 1 || !bytes.Equal(sections[0], section) {
		t.Fatal(err)
	}
	for i := 0; i < len(compressed); i++ {
		if _, err := decodeMapBundle(compressed[:i]); err == nil {
			t.Fatal("truncated gzip", i)
		}
	}
	for _, modify := range []func([]byte){
		func(b []byte) { b[4] = 2 }, func(b []byte) { b[5] = 0 }, func(b []byte) { b[5] = 13 },
		func(b []byte) { binary.BigEndian.PutUint32(b[6:], 0xffffffff) },
		func(b []byte) { binary.BigEndian.PutUint32(b[6:], 0) },
		func(b []byte) { b[15] = 1 }, // Section map ID must match catalogue ordinal.
	} {
		bad := bytes.Clone(body)
		modify(bad)
		if _, err := decodeMapBundle(zipFixture(t, bad)); err == nil {
			t.Fatal("invalid bundle")
		}
	}
	for _, bad := range [][]byte{zipFixture(t, append(bytes.Clone(body), 0)), append(bytes.Clone(compressed), 0), append(bytes.Clone(compressed), compressed...)} {
		if _, err := decodeMapBundle(bad); err == nil {
			t.Fatal("trailing compressed or decoded bytes")
		}
	}
	bad := bytes.Clone(compressed)
	bad[len(bad)-8] ^= 1
	if _, err := decodeMapBundle(bad); err == nil {
		t.Fatal("bad checksum")
	}
}

func TestInstalledMapsNeedNoOriginalInputs(t *testing.T) {
	if err := CheckMaps(); err != nil {
		t.Fatal(err)
	}
	triangles := []int{23029, 44598, 97378, 44073, 100517, 25179}
	spawns := []int{47, 32, 56, 29, 36, 23}
	modes := []uint16{254, 254, 246, 30, 118, 158}
	for id := range triangles {
		m, err := LoadMap(byte(id))
		if err != nil {
			t.Fatal(id, err)
		}
		count := 0
		for _, o := range m.Objects {
			for _, p := range o.Parts {
				count += len(p.Surfaces)
			}
		}
		if m.ID != byte(id) || m.ModeBits != modes[id] || len(m.Spawns) != spawns[id] || count != triangles[id] {
			t.Fatal("installed map content", id, count)
		}
		if len(m.Zones) == 0 {
			t.Fatal("missing installed zone identities", id)
		}
		if _, err := m.NewScene(); err != nil {
			t.Fatal("installed scene", id, err)
		}
	}
	if _, err := LoadMap(6); err == nil {
		t.Fatal("disabled map")
	}
	a, _ := LoadMap(0)
	original := a.Spawns[0].Transform
	a.Spawns[0].Transform.Position = Vec3{999, 999, 999}
	b, _ := LoadMap(0)
	if b.Spawns[0].Transform != original {
		t.Fatal("matches shared mutable data")
	}
}
