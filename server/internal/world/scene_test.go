package world

import (
	"errors"
	"math"
	"sync"
	"testing"
)

func sceneFloor(id uint32, height float64, static bool, filter Filter) Object {
	return Object{ID: id, Static: static, Enabled: true,
		Transform: Transform{Position: Vec3{0, 0, height}, Scale: Vec3{1, 1, 1}},
		Parts:     []Part{{filter, []Surface{floor(0, 999)}}}}
}

func TestSceneMovesDisablesAndOwnsGeometry(t *testing.T) {
	objects := []Object{sceneFloor(7, 0, true, MapFilter(true, 0)), sceneFloor(4, 2, false, MapFilter(false, 0))}
	s, err := NewScene(objects)
	if err != nil {
		t.Fatal(err)
	}
	objects[1].Parts[0].Surfaces[0].A = Vec3{math.NaN(), 0, 0}
	objects[1].Parts[0].Filter = Filter{}
	query := Filter{0xe001, 0xf926} // Native character controller defaults.
	ray := func(want float64, id uint32) {
		t.Helper()
		h, hit, err := s.Raycast(Vec3{0, 0, 4}, Vec3{0, 0, -4}, query, nil)
		assertHit(t, h, hit, err, want, id)
	}
	ray(.25, 4)
	pose := Transform{Position: Vec3{0, 0, -2}, Scale: Vec3{1, 1, 1}}
	if err := s.Move(4, pose); err != nil {
		t.Fatal(err)
	}
	ray(.5, 7) // The old position of the moving platform is gone.
	if err := s.Enable(7, false); err != nil {
		t.Fatal(err)
	}
	ray(.75, 4)
	h, hit, err := s.CapsuleCast(Capsule{Vec3{0, 0, 4}, Vec3{0, 0, 5}, .5}, Vec3{0, 0, -8}, query, nil)
	assertHit(t, h, hit, err, 5.5/8, 4)
	ignored := uint32(4)
	if _, hit, err := s.Raycast(Vec3{0, 0, 4}, Vec3{0, 0, -4}, query, &ignored); hit || err != nil {
		t.Fatal("self collision", hit, err)
	}
	pose.Position.X = math.Inf(1)
	if !errors.Is(s.Move(4, pose), ErrGeometry) {
		t.Fatal("accepted invalid replacement")
	}
	ray(.75, 4) // Rejected updates preserve the last valid placement.
	if !errors.Is(s.Move(7, Transform{}), ErrObject) || !errors.Is(s.Enable(123, true), ErrObject) {
		t.Fatal("static/missing object mutation")
	}
}

func TestSceneFiltersAndDeterministicTies(t *testing.T) {
	for _, static := range []bool{true, false} {
		for _, material := range []uint8{0, 17, 18, 19, 20, 255} {
			filter := MapFilter(static, material)
			want := uint16(0x20)
			if static {
				want = 2
			}
			if material == 18 || material == 19 {
				want = 0x800
			}
			if filter != (Filter{want, 0xf7d9}) || !filter.Accepts(Filter{0xe001, 0xf926}) {
				t.Fatalf("incorrect native map filter: %v %d %+v", static, material, filter)
			}
		}
	}
	s, err := NewScene([]Object{sceneFloor(10, 0, true, Filter{2, 4}), sceneFloor(1, 0, true, Filter{2, 4})})
	if err != nil {
		t.Fatal(err)
	}
	for _, query := range []Filter{{1, 2}, {4, 8}, {0, 0xffff}, {0xffff, 0}} {
		if _, hit, err := s.Raycast(Vec3{0, 0, 1}, Vec3{0, 0, -1}, query, nil); hit || err != nil {
			t.Fatalf("one-sided filter accepted: %+v", query)
		}
	}
	h, hit, err := s.Raycast(Vec3{0, 0, 1}, Vec3{0, 0, -1}, Filter{4, 2}, nil)
	assertHit(t, h, hit, err, .5, 1)
	if _, err := NewScene([]Object{sceneFloor(1, 0, true, Filter{}), sceneFloor(1, 1, true, Filter{})}); !errors.Is(err, ErrObject) {
		t.Fatal("duplicate collision owner")
	}
	empty, _ := NewScene(nil)
	if _, _, err := empty.Raycast(Vec3{math.NaN(), 0, 0}, Vec3{}, Filter{}, nil); !errors.Is(err, ErrGeometry) {
		t.Fatal("invalid empty scene query")
	}
}

func TestSceneConcurrentWholeObjectUpdates(t *testing.T) {
	s, err := NewScene([]Object{sceneFloor(1, 0, false, Filter{1, 1})})
	if err != nil {
		t.Fatal(err)
	}
	var workers sync.WaitGroup
	for i := 0; i < 4; i++ {
		workers.Add(1)
		go func(writer bool) {
			defer workers.Done()
			for j := 0; j < 100; j++ {
				if writer {
					if err := s.Move(1, Transform{Position: Vec3{0, 0, float64(j % 2)}, Scale: Vec3{1, 1, 1}}); err != nil {
						t.Error(err)
					}
				} else {
					h, hit, err := s.Raycast(Vec3{0, 0, 2}, Vec3{0, 0, -2}, Filter{1, 1}, nil)
					if err != nil || !hit || (h.Fraction != .25 && h.Fraction != .5) || h.Object != 1 {
						t.Errorf("partial update: %+v %v %v", h, hit, err)
					}
				}
			}
		}(i == 0)
	}
	workers.Wait()
}
