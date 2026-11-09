package pc

import (
	"bytes"
	"encoding/hex"
	"math"
	"reflect"
	"testing"
)

func TestSnapshotWireGoldenAndEnvelope(t *testing.T) {
	s := Snapshot{Slot: 2, Generation: 0x01020304, Sequence: 0xfffffffe, Time: 1000, Position: [3]float32{1, -2, 0.5}, Yaw: 0x4000, Pitch: 0xffff}
	body, err := EncodeSnapshots([]Snapshot{s})
	if err != nil {
		t.Fatal(err)
	}
	// Synthetic coordinates and angles; no extracted original game data.
	golden, _ := hex.DecodeString("010201020304fffffffe000003e83f800000c00000003f0000004000ffff")
	if !bytes.Equal(body, golden) {
		t.Fatalf("snapshot %x", body)
	}
	packet, err := Encode(Snapshots, body)
	if err != nil || !bytes.Equal(packet[:8], []byte{'N', '3', 'P', 'C', 1, 2, 0, 30}) {
		t.Fatal(packet, err)
	}
	kind, decoded, err := Decode(packet)
	got, readErr := DecodeSnapshots(decoded)
	if err != nil || readErr != nil || kind != Snapshots || !reflect.DeepEqual(got, []Snapshot{s}) {
		t.Fatal(got, err, readErr)
	}
	for n := 0; n < len(packet); n++ {
		if _, _, err := Decode(packet[:n]); err == nil {
			t.Fatal("accepted truncation", n)
		}
	}
	for _, at := range []int{0, 4, 5, 6, 7, 8, 9} {
		bad := bytes.Clone(packet)
		bad[at] = 255
		if _, _, err := Decode(bad); err == nil {
			t.Fatal("accepted invalid field", at)
		}
	}
	if _, _, err := Decode(append(packet, 0)); err == nil {
		t.Fatal("accepted trailing bytes")
	}
}

func TestSnapshotValidationAndAtomicReject(t *testing.T) {
	s := Snapshot{Generation: 1}
	if _, err := EncodeSnapshots([]Snapshot{s, s}); err == nil {
		t.Fatal("duplicate slot")
	}
	for _, bad := range []Snapshot{{Generation: 0}, {Slot: 12, Generation: 1}, {Generation: 1, Position: [3]float32{float32(math.NaN())}}, {Generation: 1, Position: [3]float32{0, float32(math.Inf(1))}}} {
		if _, err := EncodeSnapshots([]Snapshot{bad}); err == nil {
			t.Fatal("invalid state accepted")
		}
	}
	s2 := s
	s2.Slot = 1
	body, _ := EncodeSnapshots([]Snapshot{s, s2})
	body[1+RecordSize] = 0
	if got, err := DecodeSnapshots(body); err == nil || got != nil {
		t.Fatal("partial acceptance", got, err)
	}
	for _, body := range [][]byte{{}, {0}, {13}, {1, 2}} {
		if _, err := DecodeSnapshots(body); err == nil {
			t.Fatal("invalid count/length")
		}
	}
	for _, body := range [][]byte{{0, 0, 0, 0}, {0, 0, 0, 3}, {0, 0, 1}, {0, 0, 0, 1, 0}} {
		if _, err := Encode(Capabilities, body); err == nil {
			t.Fatal("invalid capabilities")
		}
	}
	if _, err := Encode(Capabilities, []byte{0, 0, 0, 1}); err != nil {
		t.Fatal(err)
	}
	if _, err := Encode(Native, make([]byte, MaxPayload+1)); err == nil {
		t.Fatal("oversized native payload")
	}
}

func TestAnglesPreserveFractionalDegreesAndWrap(t *testing.T) {
	for _, angle := range []float64{-720, -360, -0.1, 0, 0.1, 1.25, 90, 180, 359.999, 360, 720.1} {
		encoded, err := EncodeAngle(angle)
		if err != nil {
			t.Fatal(err)
		}
		difference := math.Remainder(DecodeAngle(encoded)-angle, 360)
		if math.Abs(difference) > 180.0/65536+1e-9 {
			t.Fatal(angle, encoded, difference)
		}
	}
	for _, bad := range []float64{math.NaN(), math.Inf(1), math.Inf(-1)} {
		if _, err := EncodeAngle(bad); err == nil {
			t.Fatal("nonfinite angle")
		}
	}
	if !Newer(0, math.MaxUint32) || Newer(math.MaxUint32, 0) || Newer(1, 1) || Newer(0x80000000, 0) {
		t.Fatal("serial ordering")
	}
}
