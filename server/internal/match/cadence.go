package match

import "time"

const TickRate = 30

// cadence retains rational absolute deadlines: no rounded 33ms drift and no
// catch-up burst of stale snapshots after scheduler stalls. Simulation may
// consume elapsed time separately; a publication is always current state.
type cadence struct {
	epoch time.Time
	next  time.Duration
}

func newCadence(now time.Time) cadence {
	// Runner and room publication must use the same rational tick boundaries.
	// Starting a private epoch at a delayed wake makes the next, less-delayed
	// runner tick appear early to that room and drops an otherwise valid update.
	// Add retains the monotonic clock; Truncate would discard it. Whole seconds
	// contain exactly TickRate ticks, so rooms created later share the grid too.
	epoch := now.Add(-time.Duration(now.Nanosecond()))
	return cadence{epoch: epoch, next: now.Sub(epoch)}
}
func (c *cadence) deadline() time.Time { return c.epoch.Add(c.next) }
func (c *cadence) due(now time.Time) bool {
	elapsed := now.Sub(c.epoch)
	if elapsed < c.next {
		return false
	}
	seconds, remainder := elapsed/time.Second, elapsed%time.Second
	index := int64(remainder)*TickRate/int64(time.Second) + 1
	// Ceiling division avoids an early fractional-nanosecond deadline.
	c.next = seconds*time.Second + time.Duration((index*int64(time.Second)+TickRate-1)/TickRate)
	return true
}
