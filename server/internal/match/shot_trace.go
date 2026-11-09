package match

import (
	"fmt"
	"nova3.test/backend/internal/world"
)

// One bounded receipt per latest discharge diagnoses the owner's reported
// accepted-but-missing impacts through the existing local combat log.
type shotTrace struct {
	projectile             int32
	from, end, target      world.Vec3
	wall                   uint32
	players, mounts, steps int
}

func (b bullet) recordTrace(from, end world.Vec3, wall uint32, players map[*peer][]world.HitRegion, mounts map[*mountState][]world.HitRegion) {
	if b.discharge == nil || b.owner.lastDischarge != b.discharge {
		return
	}
	t := &b.discharge.trace
	t.projectile, t.from, t.end, t.wall = b.projectile.TypeID, from, end, wall
	t.players, t.mounts, t.target = 0, 0, world.Vec3{}
	t.steps++
	for p, parts := range players {
		if p != b.owner && len(parts) > 0 {
			t.players += len(parts)
			t.target = parts[0].Volume.A.Add(parts[0].Volume.B).Mul(.5)
		}
	}
	for _, parts := range mounts {
		t.mounts += len(parts)
	}
}

func (t shotTrace) String() string {
	return fmt.Sprintf("p%d steps=%d from=(%.3f,%.3f,%.3f) end=(%.3f,%.3f,%.3f) wall=%d regions=%d/%d target=(%.3f,%.3f,%.3f)", t.projectile, t.steps, t.from.X, t.from.Y, t.from.Z, t.end.X, t.end.Y, t.end.Z, t.wall, t.players, t.mounts, t.target.X, t.target.Y, t.target.Z)
}
