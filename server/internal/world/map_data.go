package world

import (
	"encoding/binary"
	"errors"
	"io"
	"math"
)

var ErrMap = errors.New("invalid technical collision map")

// CollisionMap is the collision/spawn section of a server-owned map package.
// It contains no filenames, original dictionaries, scripts or render assets.
// Interactive object rules belong to the match owner, not this data decoder.
type CollisionMap struct {
	ID             uint8
	ModeBits       uint16
	Objects        []MapObject
	Spawns         []SpawnPoint
	Zones          []int32 // Original zone identities only; no scripts or presentation assets.
	PositionBounds PositionBounds
}

type MapObject struct {
	Object
	Type        uint32
	Transparent bool
}

type SpawnPoint struct {
	ID                  uint32
	Transform           Transform
	Enabled, LevelStart bool
	Team                int32
	Car, CarWaypoint    int32
	MachineGun, Bike    int32
	Mech, LinkedSpawn   int32
}

// Supports uses the original menu's game-mode-to-bit mapping. Native mode IDs
// are not bit indices (notably instagib=5 and cooperative=7).
func (m *CollisionMap) Supports(mode uint8) bool {
	bits := [...]uint16{2, 4, 8, 32, 64, 16, 128, 1}
	return int(mode) < len(bits) && m.ModeBits&bits[mode] != 0
}

func (m *CollisionMap) NewScene() (*Scene, error) {
	objects := make([]Object, len(m.Objects))
	for i := range m.Objects {
		objects[i] = m.Objects[i].Object
	}
	return NewScene(objects)
}

type mapReader struct {
	r   io.Reader
	err error
	buf [4]byte
}

func (r *mapReader) bytes(b []byte) {
	if r.err == nil {
		_, r.err = io.ReadFull(r.r, b)
	}
}
func (r *mapReader) u8() uint8   { r.bytes(r.buf[:1]); return r.buf[0] }
func (r *mapReader) u16() uint16 { r.bytes(r.buf[:2]); return binary.BigEndian.Uint16(r.buf[:2]) }
func (r *mapReader) u32() uint32 { r.bytes(r.buf[:]); return binary.BigEndian.Uint32(r.buf[:]) }
func (r *mapReader) vector() Vec3 {
	v := Vec3{float64(math.Float32frombits(r.u32())), float64(math.Float32frombits(r.u32())), float64(math.Float32frombits(r.u32()))}
	if !v.valid() {
		r.err = ErrMap
	}
	return v
}
func (r *mapReader) transform() Transform { return Transform{r.vector(), r.vector(), r.vector()} }
func (r *mapReader) count(max uint32) int {
	n := r.u32()
	if n > max || r.err != nil {
		r.err = ErrMap
		return 0
	}
	return int(n)
}

// DecodeCollisionMap is bounded even for untrusted/corrupt package bytes. A
// section is published only after its complete contents and references pass.
// The caller supplies the installation-owned section; no runtime source-game
// reader or configurable import directory is involved.
func DecodeCollisionMap(source io.Reader) (*CollisionMap, error) {
	r := mapReader{r: io.LimitReader(source, 32<<20)}
	var magic [5]byte
	r.bytes(magic[:])
	if string(magic[:]) != "NV3C\x03" {
		return nil, ErrMap
	}
	m := &CollisionMap{ID: r.u8(), ModeBits: r.u16()}
	objects, spawns := r.count(4096), r.count(512)
	if r.err != nil || m.ModeBits == 0 || m.ModeBits > 255 || spawns == 0 {
		return nil, ErrMap
	}
	ids := make(map[uint32]bool, objects+spawns)
	remaining := 500000
	for i := 0; i < objects; i++ {
		o := MapObject{}
		o.ID, o.Type = r.u32(), r.u32()
		flags := r.u8()
		o.Enabled, o.Static, o.Transparent = flags&1 != 0, flags&2 != 0, flags&4 != 0
		o.Transform = r.transform()
		faces := r.count(uint32(remaining))
		if r.err != nil || flags & ^uint8(7) != 0 || ids[o.ID] || o.ID > math.MaxInt32 {
			return nil, ErrMap
		}
		ids[o.ID] = true
		remaining -= faces
		regular := Part{Filter: MapFilter(o.Static, 0)}
		invisible := Part{Filter: MapFilter(o.Static, 18)}
		for j := 0; j < faces; j++ {
			s := Surface{A: r.vector(), B: r.vector(), C: r.vector(), Object: o.ID, Mask: All}
			s.Material = r.u8()
			if r.err != nil {
				return nil, ErrMap
			}
			if s.Material == 18 || s.Material == 19 {
				invisible.Surfaces = append(invisible.Surfaces, s)
			} else {
				regular.Surfaces = append(regular.Surfaces, s)
			}
		}
		for _, part := range []Part{regular, invisible} {
			if len(part.Surfaces) != 0 {
				o.Parts = append(o.Parts, part)
			}
		}
		m.Objects = append(m.Objects, o)
	}
	spawnIDs := make(map[uint32]bool, spawns)
	for i := 0; i < spawns; i++ {
		s := SpawnPoint{ID: r.u32(), Transform: r.transform()}
		flags := r.u8()
		s.Enabled, s.LevelStart = flags&1 != 0, flags&2 != 0
		s.Team = int32(r.u32())
		s.Car, s.CarWaypoint, s.MachineGun = int32(r.u32()), int32(r.u32()), int32(r.u32())
		s.Bike, s.Mech, s.LinkedSpawn = int32(r.u32()), int32(r.u32()), int32(r.u32())
		if r.err != nil || flags & ^uint8(3) != 0 || ids[s.ID] || s.ID > math.MaxInt32 || s.Team < 0 || s.Team > 12 ||
			s.Car < -1 || s.CarWaypoint < -1 || s.MachineGun < -1 || s.Bike < -1 || s.Mech < -1 || s.LinkedSpawn < -1 {
			return nil, ErrMap
		}
		ids[s.ID], spawnIDs[s.ID] = true, true
		m.Spawns = append(m.Spawns, s)
	}
	for _, s := range m.Spawns {
		if s.LinkedSpawn >= 0 && !spawnIDs[uint32(s.LinkedSpawn)] {
			return nil, ErrMap
		}
	}
	zoneIDs := make(map[int32]bool)
	zones := r.count(4096)
	for i := 0; i < zones; i++ {
		id := int32(r.u32())
		if r.err != nil || id < 0 || zoneIDs[id] {
			return nil, ErrMap
		}
		zoneIDs[id] = true
		m.Zones = append(m.Zones, id)
	}
	m.PositionBounds = PositionBounds{r.vector(), r.vector()}
	if !m.PositionBounds.valid() {
		return nil, ErrMap
	}
	var extra [1]byte
	n, err := io.ReadFull(source, extra[:])
	if r.err != nil || n != 0 || err != io.EOF {
		return nil, ErrMap
	}
	return m, nil
}
