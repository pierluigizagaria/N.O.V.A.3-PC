// Package world provides server-owned geometry queries. It has no dependency
// on the client, original readers, graphics, or operating system APIs.
package world

import (
	"errors"
	"math"
	"sort"
)

var (
	ErrGeometry    = errors.New("invalid world geometry or query")
	ErrConvergence = errors.New("world sweep failed to converge")
)

// Vec3 uses the game's Z-up world coordinates. Double precision is used for
// intermediate geometry even though the original source coordinates are floats.
type Vec3 struct{ X, Y, Z float64 }

func (a Vec3) Add(b Vec3) Vec3    { return Vec3{a.X + b.X, a.Y + b.Y, a.Z + b.Z} }
func (a Vec3) Sub(b Vec3) Vec3    { return Vec3{a.X - b.X, a.Y - b.Y, a.Z - b.Z} }
func (a Vec3) Mul(s float64) Vec3 { return Vec3{a.X * s, a.Y * s, a.Z * s} }
func (a Vec3) Dot(b Vec3) float64 { return a.X*b.X + a.Y*b.Y + a.Z*b.Z }
func (a Vec3) Cross(b Vec3) Vec3 {
	return Vec3{a.Y*b.Z - a.Z*b.Y, a.Z*b.X - a.X*b.Z, a.X*b.Y - a.Y*b.X}
}
func (a Vec3) length() float64 { return math.Sqrt(a.Dot(a)) }
func (a Vec3) unit() Vec3 {
	if n := a.length(); n > 0 {
		return a.Mul(1 / n)
	}
	return Vec3{}
}
func (a Vec3) axis(i int) float64 {
	if i == 0 {
		return a.X
	}
	if i == 1 {
		return a.Y
	}
	return a.Z
}
func valid(v float64) bool            { return !math.IsNaN(v) && !math.IsInf(v, 0) && math.Abs(v) <= 1e9 }
func (a Vec3) valid() bool            { return valid(a.X) && valid(a.Y) && valid(a.Z) }
func clamp(v, lo, hi float64) float64 { return math.Max(lo, math.Min(hi, v)) }

// Masks are assigned by the world importer from verified object/material
// semantics, never inferred from render visibility or from a numeric material.
type Mask uint32

const (
	Movement Mask = 1 << iota
	Projectile
	All = Movement | Projectile
)

type Surface struct {
	A, B, C  Vec3
	Object   uint32
	Material uint8
	Mask     Mask
}

type Hit struct {
	Fraction      float64 // [0,1] along the requested displacement
	Point, Normal Vec3    // Point is on the surface, normal faces the moving shape.
	Object        uint32
	Material      uint8
}

type bounds struct{ min, max Vec3 }

func box(a, b Vec3) bounds {
	return bounds{Vec3{math.Min(a.X, b.X), math.Min(a.Y, b.Y), math.Min(a.Z, b.Z)}, Vec3{math.Max(a.X, b.X), math.Max(a.Y, b.Y), math.Max(a.Z, b.Z)}}
}
func (b bounds) include(v Vec3) bounds { return bounds{box(b.min, v).min, box(b.max, v).max} }
func (b bounds) union(c bounds) bounds { return b.include(c.min).include(c.max) }
func (b bounds) expand(r float64) bounds {
	d := Vec3{r, r, r}
	return bounds{b.min.Sub(d), b.max.Add(d)}
}
func (b bounds) overlaps(c bounds) bool {
	return b.min.X <= c.max.X && b.max.X >= c.min.X && b.min.Y <= c.max.Y && b.max.Y >= c.min.Y && b.min.Z <= c.max.Z && b.max.Z >= c.min.Z
}
func (b bounds) segment(a, d Vec3, limit float64) bool {
	lo, hi := 0.0, limit
	for axis := 0; axis < 3; axis++ {
		origin, delta := a.axis(axis), d.axis(axis)
		if delta == 0 {
			if origin < b.min.axis(axis) || origin > b.max.axis(axis) {
				return false
			}
			continue
		}
		u, v := (b.min.axis(axis)-origin)/delta, (b.max.axis(axis)-origin)/delta
		if u > v {
			u, v = v, u
		}
		lo = math.Max(lo, u)
		hi = math.Min(hi, v)
		if lo > hi {
			return false
		}
	}
	return true
}

type triangle struct {
	Surface
	bounds  bounds
	ordinal int
}
type node struct {
	bounds                  bounds
	start, end, left, right int
}

// Mesh owns an immutable copy. Queries may run concurrently. Build a separate
// mesh for moving objects at their authoritative transform; never bake their
// initial placement into permanent static collision.
type Mesh struct {
	triangles []triangle
	nodes     []node
}

func NewMesh(surfaces []Surface) (*Mesh, error) {
	m := &Mesh{}
	for i, s := range surfaces {
		if !s.A.valid() || !s.B.valid() || !s.C.valid() || s.Mask & ^All != 0 {
			return nil, ErrGeometry
		}
		// Exporters can contain zero-area faces and disabled collision. Neither
		// defines a surface, but all coordinates must still pass validation.
		if s.Mask == 0 || s.B.Sub(s.A).Cross(s.C.Sub(s.A)).length() == 0 {
			continue
		}
		m.triangles = append(m.triangles, triangle{s, box(s.A, s.B).include(s.C), i})
	}
	if len(m.triangles) > 0 {
		m.build(0, len(m.triangles))
	}
	return m, nil
}

func (m *Mesh) build(start, end int) int {
	b := m.triangles[start].bounds
	for i := start + 1; i < end; i++ {
		b = b.union(m.triangles[i].bounds)
	}
	index := len(m.nodes)
	m.nodes = append(m.nodes, node{b, start, end, -1, -1})
	if end-start <= 8 {
		return index
	}
	extent := b.max.Sub(b.min)
	axis := 0
	if extent.Y > extent.X {
		axis = 1
	}
	if extent.Z > extent.axis(axis) {
		axis = 2
	}
	part := m.triangles[start:end]
	sort.Slice(part, func(i, j int) bool {
		a, c := part[i].bounds, part[j].bounds
		u, v := a.min.axis(axis)+a.max.axis(axis), c.min.axis(axis)+c.max.axis(axis)
		if u == v {
			return part[i].ordinal < part[j].ordinal
		}
		return u < v
	})
	mid := start + (end-start)/2
	left, right := m.build(start, mid), m.build(mid, end)
	m.nodes[index].left, m.nodes[index].right = left, right
	return index
}

// segmentTriangle is two-sided. A coplanar segment does not cross a face;
// overlap and volume sweeps are handled by CapsuleCast instead.
func segmentTriangle(a, d Vec3, t Surface, limit float64) (float64, bool) {
	e1, e2 := t.B.Sub(t.A), t.C.Sub(t.A)
	p := d.Cross(e2)
	det := e1.Dot(p)
	if det == 0 {
		return 0, false
	}
	s := a.Sub(t.A)
	u := s.Dot(p) / det
	if u < 0 || u > 1 {
		return 0, false
	}
	q := s.Cross(e1)
	v := d.Dot(q) / det
	if v < 0 || u+v > 1 {
		return 0, false
	}
	f := e2.Dot(q) / det
	return f, f >= 0 && f <= limit
}

func (m *Mesh) Raycast(from, to Vec3, mask Mask) (Hit, bool, error) {
	if !from.valid() || !to.valid() || mask == 0 || mask & ^All != 0 {
		return Hit{}, false, ErrGeometry
	}
	if len(m.nodes) == 0 || from == to {
		return Hit{}, false, nil
	}
	d := to.Sub(from)
	best := Hit{Fraction: 1}
	found := false
	order := int(^uint(0) >> 1)
	stack := []int{0}
	for len(stack) > 0 {
		i := stack[len(stack)-1]
		stack = stack[:len(stack)-1]
		n := m.nodes[i]
		if !n.bounds.segment(from, d, best.Fraction) {
			continue
		}
		if n.left >= 0 {
			stack = append(stack, n.left, n.right)
			continue
		}
		for _, t := range m.triangles[n.start:n.end] {
			if t.Mask&mask == 0 {
				continue
			}
			f, ok := segmentTriangle(from, d, t.Surface, best.Fraction)
			if !ok || (found && f == best.Fraction && t.ordinal > order) {
				continue
			}
			normal := t.B.Sub(t.A).Cross(t.C.Sub(t.A)).unit()
			if normal.Dot(d) > 0 {
				normal = normal.Mul(-1)
			}
			best = Hit{f, from.Add(d.Mul(f)), normal, t.Object, t.Material}
			found = true
			order = t.ordinal
		}
	}
	return best, found, nil
}

// Capsule is a line segment swept by a radius; coincident endpoints define a
// sphere. Keeping the axis explicit also supports posed vehicles/actors without
// assuming a world-up capsule in the collision kernel.
type Capsule struct {
	A, B   Vec3
	Radius float64
}

func closestTriangle(p Vec3, t Surface) Vec3 {
	ab, ac, ap := t.B.Sub(t.A), t.C.Sub(t.A), p.Sub(t.A)
	d1, d2 := ab.Dot(ap), ac.Dot(ap)
	if d1 <= 0 && d2 <= 0 {
		return t.A
	}
	bp := p.Sub(t.B)
	d3, d4 := ab.Dot(bp), ac.Dot(bp)
	if d3 >= 0 && d4 <= d3 {
		return t.B
	}
	vc := d1*d4 - d3*d2
	if vc <= 0 && d1 >= 0 && d3 <= 0 {
		return t.A.Add(ab.Mul(d1 / (d1 - d3)))
	}
	cp := p.Sub(t.C)
	d5, d6 := ab.Dot(cp), ac.Dot(cp)
	if d6 >= 0 && d5 <= d6 {
		return t.C
	}
	vb := d5*d2 - d1*d6
	if vb <= 0 && d2 >= 0 && d6 <= 0 {
		return t.A.Add(ac.Mul(d2 / (d2 - d6)))
	}
	va := d3*d6 - d5*d4
	if va <= 0 && d4-d3 >= 0 && d5-d6 >= 0 {
		return t.B.Add(t.C.Sub(t.B).Mul((d4 - d3) / ((d4 - d3) + (d5 - d6))))
	}
	denom := 1 / (va + vb + vc)
	return t.A.Add(ab.Mul(vb * denom)).Add(ac.Mul(vc * denom))
}

func closestSegments(a, b, c, d Vec3) (Vec3, Vec3) {
	u, v, w := b.Sub(a), d.Sub(c), a.Sub(c)
	aa, bb, cc := u.Dot(u), u.Dot(v), v.Dot(v)
	dd, ee := u.Dot(w), v.Dot(w)
	s, t := 0.0, 0.0
	if aa == 0 {
		if cc > 0 {
			t = clamp(ee/cc, 0, 1)
		}
	} else if cc == 0 {
		s = clamp(-dd/aa, 0, 1)
	} else {
		denom := aa*cc - bb*bb
		if denom > 0 {
			s = clamp((bb*ee-cc*dd)/denom, 0, 1)
		}
		t = (bb*s + ee) / cc
		if t < 0 {
			t = 0
			s = clamp(-dd/aa, 0, 1)
		} else if t > 1 {
			t = 1
			s = clamp((bb-dd)/aa, 0, 1)
		}
	}
	return a.Add(u.Mul(s)), c.Add(v.Mul(t))
}

func closestAxis(a, b Vec3, t Surface) (Vec3, Vec3) {
	if f, ok := segmentTriangle(a, b.Sub(a), t, 1); ok {
		p := a.Add(b.Sub(a).Mul(f))
		return p, p
	}
	p, q := a, closestTriangle(a, t)
	distance := p.Sub(q).Dot(p.Sub(q))
	consider := func(x, y Vec3) {
		d := x.Sub(y).Dot(x.Sub(y))
		if d < distance {
			p, q, distance = x, y, d
		}
	}
	consider(b, closestTriangle(b, t))
	for _, edge := range [][2]Vec3{{t.A, t.B}, {t.B, t.C}, {t.C, t.A}} {
		x, y := closestSegments(a, b, edge[0], edge[1])
		consider(x, y)
	}
	return p, q
}

func sweep(c Capsule, d Vec3, t Surface, limit float64) (Hit, bool, error) {
	f := 0.0
	// Conservative advancement uses the separating plane of the closest
	// features. It cannot jump across a thin triangle at high speed. The
	// tolerance is a world-space contact distance, not a fraction of velocity.
	const tolerance = 1e-7
	for iteration := 0; iteration < 64; iteration++ {
		offset := d.Mul(f)
		p, q := closestAxis(c.A.Add(offset), c.B.Add(offset), t)
		delta := p.Sub(q)
		distance := delta.length()
		gap := distance - c.Radius
		normal := delta.unit()
		if gap <= tolerance {
			if distance == 0 {
				normal = t.B.Sub(t.A).Cross(t.C.Sub(t.A)).unit()
				if normal.Dot(d) > 0 {
					normal = normal.Mul(-1)
				}
			}
			return Hit{f, q, normal, t.Object, t.Material}, true, nil
		}
		closing := -normal.Dot(d)
		if closing <= 0 {
			return Hit{}, false, nil
		}
		next := f + gap/closing
		if next > limit {
			return Hit{}, false, nil
		}
		if next <= f {
			return Hit{}, false, ErrConvergence
		}
		f = next
	}
	return Hit{}, false, ErrConvergence
}

// CapsuleCast returns initial overlap at fraction zero, including stationary
// queries. The caller owns support-contact/slide policy; this kernel never
// invents a movement rule or silently accepts a failed sweep.
func (m *Mesh) CapsuleCast(c Capsule, displacement Vec3, mask Mask) (Hit, bool, error) {
	if !c.A.valid() || !c.B.valid() || !displacement.valid() || !valid(c.Radius) || c.Radius <= 0 || !c.A.Add(displacement).valid() || !c.B.Add(displacement).valid() || mask == 0 || mask & ^All != 0 {
		return Hit{}, false, ErrGeometry
	}
	if len(m.nodes) == 0 {
		return Hit{}, false, nil
	}
	query := box(c.A, c.B).union(box(c.A.Add(displacement), c.B.Add(displacement))).expand(c.Radius + 1e-7)
	best := Hit{Fraction: 1}
	found := false
	order := int(^uint(0) >> 1)
	stack := []int{0}
	for len(stack) > 0 {
		i := stack[len(stack)-1]
		stack = stack[:len(stack)-1]
		n := m.nodes[i]
		if !n.bounds.overlaps(query) {
			continue
		}
		if n.left >= 0 {
			stack = append(stack, n.left, n.right)
			continue
		}
		for _, t := range m.triangles[n.start:n.end] {
			if t.Mask&mask == 0 || !t.bounds.overlaps(query) {
				continue
			}
			hit, ok, err := sweep(c, displacement, t.Surface, best.Fraction)
			if err != nil {
				return Hit{}, false, err
			}
			if !ok || (found && hit.Fraction == best.Fraction && t.ordinal > order) {
				continue
			}
			best, found, order = hit, true, t.ordinal
		}
	}
	return best, found, nil
}
