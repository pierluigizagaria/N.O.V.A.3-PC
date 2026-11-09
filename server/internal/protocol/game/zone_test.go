package game

import (
	"bytes"
	"testing"
)

func TestZoneChangeOriginalBuilderAndSequence(t *testing.T) {
	v := ZoneChange{Actor: 2, Object: 2, Zone: 0x01020304, Sequence: 253}
	m, err := v.Message()
	if err != nil || !bytes.Equal(m, []byte{107, 2, 2, 1, 2, 3, 4, 253}) {
		t.Fatal(m, err)
	}
	got, err := ReadZoneChange(m)
	if err != nil || got != v {
		t.Fatal(got, err)
	}
	for _, pair := range [][2]byte{{0, 253}, {1, 0}, {253, 252}, {4, 2}} {
		if !NewerZoneSequence(pair[0], pair[1]) || NewerZoneSequence(pair[1], pair[0]) {
			t.Fatal(pair)
		}
	}
	for _, pair := range [][2]byte{{0, 0}, {253, 253}, {254, 1}, {1, 255}, {127, 0}} {
		if NewerZoneSequence(pair[0], pair[1]) {
			t.Fatal(pair)
		}
	}
	for _, bad := range []ZoneChange{{Actor: 12}, {Zone: -1}, {Sequence: 254}} {
		if _, err := bad.Message(); err == nil {
			t.Fatal(bad)
		}
	}
}
