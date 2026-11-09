package world

import (
	"errors"
	"math"
	"math/rand/v2"
	"sync"
	"testing"
)

func near(t *testing.T, a, b, tolerance float64) {
	t.Helper()
	if math.Abs(a-b) > tolerance {
		t.Fatalf("got %.12g, want %.12g (tolerance %g)", a, b, tolerance)
	}
}
func mesh(t *testing.T, s ...Surface) *Mesh {
	t.Helper()
	m, err := NewMesh(s)
	if err != nil {
		t.Fatal(err)
	}
	return m
}
func floor(z float64, id uint32) Surface {
	return Surface{A: Vec3{-100, -100, z}, B: Vec3{100, -100, z}, C: Vec3{0, 100, z}, Object: id, Material: 18, Mask: All}
}
func assertHit(t *testing.T, h Hit, ok bool, err error, fraction float64, id uint32) {
	t.Helper()
	if err != nil || !ok {
		t.Fatalf("missing hit: %+v %v %v", h, ok, err)
	}
	near(t, h.Fraction, fraction, 1e-6)
	if h.Object != id {
		t.Fatalf("wrong object: %+v", h)
	}
}

func TestRayNearestTwoSidedMasksAndOwnership(t *testing.T) {
	surfaces := []Surface{floor(-1, 1), floor(0, 2), floor(1, 3)}
	surfaces[2].Mask = Movement
	m := mesh(t, surfaces...)
	surfaces[1].A = Vec3{999, 999, 999} // caller mutations cannot alter the mesh
	h, ok, err := m.Raycast(Vec3{0, 0, 2}, Vec3{0, 0, -2}, Projectile)
	assertHit(t, h, ok, err, .5, 2)
	if h.Normal != (Vec3{0, 0, 1}) || h.Material != 18 {
		t.Fatal(h)
	}
	h, ok, err = m.Raycast(Vec3{0, 0, -2}, Vec3{0, 0, 2}, All)
	assertHit(t, h, ok, err, .25, 1)
	if h.Normal != (Vec3{0, 0, -1}) {
		t.Fatal(h)
	}
	for _, pair := range [][2]Vec3{{{0, 0, 2}, {1, 0, 2}}, {{1000, 1000, 2}, {1000, 1000, -2}}, {{0, 0, 2}, {0, 0, 2}}} {
		_, hit, e := m.Raycast(pair[0], pair[1], All)
		if hit || e != nil {
			t.Fatal("unexpected intersection", pair, e)
		}
	}
}

func TestCapsuleFastThinWallAndTallAxis(t *testing.T) {
	m := mesh(t, floor(0, 7))
	// A tiny radius and a 2000-unit displacement must not tunnel through a
	// zero-thickness triangle. The expected time is analytic plane contact.
	c := Capsule{Vec3{0, 0, 1000}, Vec3{0, 0, 1002}, .125}
	h, ok, err := m.CapsuleCast(c, Vec3{0, 0, -2000}, All)
	assertHit(t, h, ok, err, (1000-.125)/2000, 7)
	near(t, h.Point.Z, 0, 1e-10)
	near(t, h.Normal.Z, 1, 1e-10)
	// Only the middle of this capsule hits: neither endpoint sphere does.
	wall := Surface{A: Vec3{0, -1, -1}, B: Vec3{0, 1, -1}, C: Vec3{0, 0, 1}, Mask: All, Object: 8}
	m = mesh(t, wall)
	h, ok, err = m.CapsuleCast(Capsule{Vec3{-2, 0, -10}, Vec3{-2, 0, 10}, .5}, Vec3{4, 0, 0}, Movement)
	assertHit(t, h, ok, err, .375, 8)
	near(t, h.Normal.X, -1, 1e-6)
}

func TestCapsuleEdgeVertexTangencyAndInitialOverlap(t *testing.T) {
	m := mesh(t, Surface{A: Vec3{0, 0, 0}, B: Vec3{10, 0, 0}, C: Vec3{0, 10, 0}, Mask: All, Object: 4})
	// Sphere outside the triangular face hits its edge, not the plane at .4.
	c := Capsule{Vec3{5, -.6, 5}, Vec3{5, -.6, 5}, 1}
	h, ok, err := m.CapsuleCast(c, Vec3{0, 0, -10}, All)
	assertHit(t, h, ok, err, .42, 4)
	near(t, h.Point.Y, 0, 1e-7)
	c.A, c.B = Vec3{-.6, -.6, 5}, Vec3{-.6, -.6, 5}
	h, ok, err = m.CapsuleCast(c, Vec3{0, 0, -10}, All)
	assertHit(t, h, ok, err, (5-math.Sqrt(.28))/10, 4)
	if h.Point != (Vec3{}) {
		t.Fatal(h)
	}
	c.A, c.B = Vec3{5, -1, 5}, Vec3{5, -1, 5}
	h, ok, err = m.CapsuleCast(c, Vec3{0, 0, -10}, All)
	// Contact tolerance gives a conservative result near an exact tangent.
	if err != nil || !ok || h.Object != 4 || h.Fraction > .5 {
		t.Fatal("tangency was missed or crossed", h, ok, err)
	}
	near(t, h.Fraction, .5, math.Sqrt(2e-7)/10)
	c.A, c.B = Vec3{2, 2, .5}, Vec3{2, 2, 2}
	h, ok, err = m.CapsuleCast(c, Vec3{}, All)
	assertHit(t, h, ok, err, 0, 4)
	c.A, c.B = Vec3{2, 2, 2}, Vec3{2, 2, 3}
	_, ok, err = m.CapsuleCast(c, Vec3{0, 0, 1}, All)
	if ok || err != nil {
		t.Fatal("separating capsule collided", err)
	}
}

func TestBVHNearestAgainstAnalyticStack(t *testing.T) {
	var surfaces []Surface
	for i := 0; i < 513; i++ {
		surfaces = append(surfaces, floor(float64(i), uint32(i+1)))
	}
	rng := rand.New(rand.NewPCG(3, 7))
	rng.Shuffle(len(surfaces), func(i, j int) { surfaces[i], surfaces[j] = surfaces[j], surfaces[i] })
	m := mesh(t, surfaces...)
	for i := 0; i < 100; i++ {
		z := float64(rng.IntN(500)) + .5
		expected := math.Floor(z)
		h, ok, err := m.Raycast(Vec3{0, 0, z}, Vec3{0, 0, -10}, All)
		assertHit(t, h, ok, err, (z-expected)/(z+10), uint32(expected+1))
		h, ok, err = m.CapsuleCast(Capsule{Vec3{0, 0, z}, Vec3{0, 0, z}, .1}, Vec3{0, 0, -1000}, All)
		assertHit(t, h, ok, err, (z-expected-.1)/1000, uint32(expected+1))
	}
}

func TestCapsuleRandomPlanesTranslationAndAxisReversal(t *testing.T) {
	rng := rand.New(rand.NewPCG(19, 29))
	for i := 0; i < 200; i++ {
		z := rng.Float64()*1000 - 500
		radius := .01 + rng.Float64()*2
		bottom := z + radius + 1 + rng.Float64()*10
		travel := 20 + radius
		m := mesh(t, floor(z, 1))
		c := Capsule{Vec3{0, 0, bottom}, Vec3{0, 0, bottom + 3}, radius}
		for j := 0; j < 2; j++ {
			h, ok, err := m.CapsuleCast(c, Vec3{0, 0, -travel}, Movement)
			assertHit(t, h, ok, err, (bottom-z-radius)/travel, 1)
			c.A, c.B = c.B, c.A
		}
	}
}

func TestEmptyDegenerateInvalidAndConcurrent(t *testing.T) {
	m := mesh(t, Surface{Mask: All})
	_, ok, err := m.CapsuleCast(Capsule{Radius: 1}, Vec3{}, All)
	if ok || err != nil {
		t.Fatal("empty mesh", err)
	}
	bad := []float64{math.NaN(), math.Inf(1), -math.Inf(1), 1e10}
	for _, v := range bad {
		s := floor(0, 1)
		s.A.X = v
		if _, e := NewMesh([]Surface{s}); !errors.Is(e, ErrGeometry) {
			t.Fatal("invalid mesh accepted", v)
		}
		if _, _, e := m.Raycast(Vec3{v, 0, 0}, Vec3{}, All); !errors.Is(e, ErrGeometry) {
			t.Fatal("invalid ray accepted", v)
		}
		if _, _, e := m.CapsuleCast(Capsule{Radius: v}, Vec3{}, All); !errors.Is(e, ErrGeometry) {
			t.Fatal("invalid radius accepted", v)
		}
	}
	if _, _, e := m.CapsuleCast(Capsule{}, Vec3{}, All); e == nil {
		t.Fatal("zero radius accepted")
	}
	if _, _, e := m.Raycast(Vec3{}, Vec3{}, Mask(8)); e == nil {
		t.Fatal("unknown mask accepted")
	}
	m = mesh(t, floor(0, 1))
	var wg sync.WaitGroup
	for i := 0; i < 8; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := 0; j < 100; j++ {
				h, hit, e := m.Raycast(Vec3{0, 0, 1}, Vec3{0, 0, -1}, All)
				if e != nil || !hit || h.Fraction != .5 {
					t.Error("concurrent query changed mesh")
				}
			}
		}()
	}
	wg.Wait()
}
