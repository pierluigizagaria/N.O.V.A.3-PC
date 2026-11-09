package world

import (
	"bytes"
	"compress/gzip"
	_ "embed"
	"encoding/binary"
	"io"
	"sync"
)

// mapsData contains only the owner-approved technical collision/spawn sections.
// Original archives, client libraries and development readers are not runtime
// dependencies. There is no operator path or catalogue selector for this data.
//
//go:embed maps.bin.gz
var mapsData []byte

var mapSections = sync.OnceValues(func() ([][]byte, error) { return decodeMapBundle(mapsData) })

var checkMaps = sync.OnceValue(func() error {
	sections, err := mapSections()
	if err != nil {
		return err
	}
	for _, section := range sections {
		if _, err := DecodeCollisionMap(bytes.NewReader(section)); err != nil {
			return err
		}
	}
	return nil
})

// CheckMaps participates in normal installation validation before listeners
// open. The package is part of the server executable, with no external path.
func CheckMaps() error { return checkMaps() }

func decodeMapBundle(compressed []byte) ([][]byte, error) {
	source := bytes.NewReader(compressed)
	zip, err := gzip.NewReader(source)
	if err != nil {
		return nil, ErrMap
	}
	defer zip.Close()
	zip.Multistream(false)
	reader := io.LimitReader(zip, 64<<20)
	var header [6]byte
	if _, err = io.ReadFull(reader, header[:]); err != nil || string(header[:5]) != "NV3W\x01" || header[5] == 0 || header[5] > 12 {
		return nil, ErrMap
	}
	sections := make([][]byte, int(header[5]))
	remaining := uint32((64 << 20) - len(header))
	for i := range sections {
		var length [4]byte
		if _, err = io.ReadFull(reader, length[:]); err != nil {
			return nil, ErrMap
		}
		n := binary.BigEndian.Uint32(length[:])
		if n < 16 || n > 32<<20 || remaining < 4 || n > remaining-4 {
			return nil, ErrMap
		}
		remaining -= n + 4
		section := make([]byte, n)
		if _, err = io.ReadFull(reader, section); err != nil || string(section[:5]) != "NV3C\x03" || section[5] != byte(i) {
			return nil, ErrMap
		}
		sections[i] = section
	}
	var tail [1]byte
	if n, err := io.ReadFull(zip, tail[:]); n != 0 || err != io.EOF || source.Len() != 0 {
		return nil, ErrMap
	}
	return sections, nil
}

// LoadMap returns an independently owned map. Mutable scene/object state is
// never shared between matches, even when both rooms selected the same map.
func LoadMap(id byte) (*CollisionMap, error) {
	sections, err := mapSections()
	if err != nil || int(id) >= len(sections) {
		return nil, ErrMap
	}
	return DecodeCollisionMap(bytes.NewReader(sections[id]))
}
