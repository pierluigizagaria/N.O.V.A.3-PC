package match

import (
	"testing"
	"time"
)

func TestThirtyHzAbsoluteDeadlinesWithoutDriftOrBacklog(t *testing.T) {
	epoch := time.Unix(0, 0)
	c := newCadence(epoch)
	if !c.due(epoch) {
		t.Fatal("first publication missing")
	}
	for i := int64(1); i <= 30*3600; i++ {
		expected := epoch.Add(time.Duration((i*int64(time.Second) + 29) / 30))
		if c.deadline() != expected || c.due(expected.Add(-time.Nanosecond)) || !c.due(expected) || c.due(expected) {
			t.Fatal("cadence drift/duplicate", i, c.deadline(), expected)
		}
	}
	stall := epoch.Add(7200*time.Second + 50*time.Millisecond)
	if !c.due(stall) || c.due(stall) || !c.deadline().After(stall) || c.deadline().Sub(stall) > 34*time.Millisecond {
		t.Fatal("stall replay or drift")
	}
	if c.due(epoch) {
		t.Fatal("backward clock produced a snapshot")
	}
}

func TestRoomPublicationSharesRunnerDeadlinesDespiteWakeJitter(t *testing.T) {
	start := time.Unix(1730000000, 17000000)
	runner := newCadence(start)
	var rooms []cadence
	for tick := 0; tick < 300; tick++ {
		// A delayed first wake must not become each room's private clock phase.
		// Subsequent ordinary wakes alternate on either side of that delay.
		jitter := time.Millisecond
		if tick%2 == 0 {
			jitter = 8 * time.Millisecond
		}
		now := runner.deadline().Add(jitter)
		if !runner.due(now) {
			t.Fatal("runner did not tick")
		}
		if tick == 0 || tick == 31 {
			rooms = append(rooms, newCadence(now))
		}
		for i := range rooms {
			if !rooms[i].due(now) {
				t.Fatalf("room %d skipped runner tick %d", i, tick)
			}
			if rooms[i].due(now) {
				t.Fatalf("room %d published twice at tick %d", i, tick)
			}
		}
	}
	for i := range rooms {
		if rooms[i].deadline() != runner.deadline() {
			t.Fatalf("room %d drifted from the runner", i)
		}
	}
}
