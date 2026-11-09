package authority

import (
	"testing"
	"time"
)

func TestFireIntervalAndBoundedBatching(t *testing.T) {
	w := fixture().Weapons[0]
	m := WeaponModifiers{FireSpeed: 1.25, LauncherDelay: .5}
	interval, err := w.FireInterval(0, m)
	if err != nil || interval != 120*time.Millisecond {
		t.Fatal(interval, err)
	}
	w.TypeID = 23092
	interval, err = w.FireInterval(1, m)
	if err != nil || interval != 125*time.Millisecond {
		t.Fatal(interval, err)
	}
	var gate FireGate
	now := time.Unix(100, 0)
	for range 2 {
		if !gate.Allows(now, interval) {
			t.Fatal("batch rejected")
		}
		gate.Commit(now, interval)
	}
	if gate.Allows(now, interval) || gate.Allows(now.Add(interval-time.Nanosecond), interval) {
		t.Fatal("rate exceeded")
	}
	if !gate.Allows(now.Add(interval), interval) {
		t.Fatal("budget not replenished")
	}
	now = now.Add(time.Hour)
	for range 2 {
		gate.Commit(now, interval)
	}
	if gate.Allows(now, interval) {
		t.Fatal("idle accumulated unlimited budget")
	}
	if _, err := w.FireInterval(2, m); err == nil {
		t.Fatal("unknown mode")
	}
	m.FireSpeed = 0
	if _, err := w.FireInterval(0, m); err == nil {
		t.Fatal("zero speed")
	}
}
