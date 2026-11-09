package world

import (
	"errors"
	"sort"
	"sync"
)

var ErrObject = errors.New("invalid or immutable world object")

// Filter retains both sides of the native broadphase contract. A query mask
// alone is insufficient: the object must also accept the query's group.
type Filter struct{ Group, Mask uint16 }

func (f Filter) Accepts(other Filter) bool {
	return f.Group&other.Mask != 0 && other.Group&f.Mask != 0
}

// MapFilter describes the original default triangle-mesh filters. Runtime
// overrides (vehicles, disabled collision, etc.) must be supplied explicitly.
// Invisible collision materials still block character movement.
func MapFilter(static bool, material uint8) Filter {
	group := uint16(0x20)
	if static {
		group = 2
	}
	if material == 18 || material == 19 {
		group = 0x800
	}
	return Filter{group, 0xf7d9}
}

type Part struct {
	Filter   Filter
	Surfaces []Surface
}

// Object holds collision only. Static means the transform cannot change;
// Enabled remains mutable because gameplay may remove even a static obstacle.
type Object struct {
	ID        uint32
	Static    bool
	Enabled   bool
	Transform Transform
	Parts     []Part
}

type placedPart struct {
	filter Filter
	mesh   *Mesh
}

type sceneObject struct {
	object Object
	parts  []placedPart
}

// Scene owns every local mesh and its authoritative placement. Updates replace
// complete objects atomically; queries cannot observe half an obstacle move.
// The small map object set is traversed in ID order, with a BVH per mesh part.
// This avoids rebuilding an entire map when one door or platform changes.
type Scene struct {
	mu      sync.RWMutex
	objects map[uint32]*sceneObject
	order   []uint32
}

func placeObject(o Object) ([]placedPart, error) {
	if !o.Transform.valid() {
		return nil, ErrGeometry
	}
	parts := make([]placedPart, len(o.Parts))
	for i, part := range o.Parts {
		placed, err := o.Transform.Place(part.Surfaces)
		if err != nil {
			return nil, err
		}
		for j := range placed {
			placed[j].Object = o.ID
			placed[j].Mask = All // Scene uses the two-sided filter above.
		}
		mesh, err := NewMesh(placed)
		if err != nil {
			return nil, err
		}
		parts[i] = placedPart{part.Filter, mesh}
	}
	return parts, nil
}

func NewScene(objects []Object) (*Scene, error) {
	s := &Scene{objects: make(map[uint32]*sceneObject, len(objects))}
	for _, o := range objects {
		if _, exists := s.objects[o.ID]; exists {
			return nil, ErrObject
		}
		parts, err := placeObject(o)
		if err != nil {
			return nil, err
		}
		local := make([]Part, len(o.Parts))
		for i, part := range o.Parts {
			local[i] = Part{part.Filter, append([]Surface(nil), part.Surfaces...)}
		}
		o.Parts = local
		s.objects[o.ID] = &sceneObject{o, parts}
		s.order = append(s.order, o.ID)
	}
	sort.Slice(s.order, func(i, j int) bool { return s.order[i] < s.order[j] })
	return s, nil
}

func (s *Scene) Move(id uint32, transform Transform) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	entry, exists := s.objects[id]
	if !exists || entry.object.Static {
		return ErrObject
	}
	next := entry.object
	next.Transform = transform
	parts, err := placeObject(next)
	if err != nil {
		return err
	}
	entry.object, entry.parts = next, parts
	return nil
}

func (s *Scene) Enable(id uint32, enabled bool) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	entry, exists := s.objects[id]
	if !exists {
		return ErrObject
	}
	entry.object.Enabled = enabled
	return nil
}

func (s *Scene) query(filter Filter, ignore *uint32, visit func(*Mesh) (Hit, bool, error)) (Hit, bool, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	var best Hit
	found := false
	for _, id := range s.order {
		entry := s.objects[id]
		if !entry.object.Enabled || (ignore != nil && id == *ignore) {
			continue
		}
		for _, part := range entry.parts {
			if !filter.Accepts(part.filter) {
				continue
			}
			hit, ok, err := visit(part.mesh)
			if err != nil {
				return Hit{}, false, err
			}
			if ok && (!found || hit.Fraction < best.Fraction) {
				best, found = hit, true
			}
		}
	}
	return best, found, nil
}

func (s *Scene) Raycast(from, to Vec3, filter Filter, ignore *uint32) (Hit, bool, error) {
	if !from.valid() || !to.valid() {
		return Hit{}, false, ErrGeometry
	}
	return s.query(filter, ignore, func(m *Mesh) (Hit, bool, error) { return m.Raycast(from, to, All) })
}

func (s *Scene) CapsuleCast(c Capsule, delta Vec3, filter Filter, ignore *uint32) (Hit, bool, error) {
	if !c.A.valid() || !c.B.valid() || !valid(c.Radius) || c.Radius <= 0 || !delta.valid() || !c.A.Add(delta).valid() || !c.B.Add(delta).valid() {
		return Hit{}, false, ErrGeometry
	}
	return s.query(filter, ignore, func(m *Mesh) (Hit, bool, error) { return m.CapsuleCast(c, delta, All) })
}
