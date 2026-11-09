package pc

import (
	"bytes"
	"encoding/hex"
	"reflect"
	"testing"
)

func TestLifeDeclarationContract(t *testing.T) {
	records := []Life{{Slot: 2, Generation: 0x01020304}, {Slot: 11, Generation: 0xffffffff}}
	body, err := EncodeLives(records)
	if err != nil || hex.EncodeToString(body) != "0202010203040bffffffff" {
		t.Fatal(body, err)
	}
	packet, err := Encode(Lives, body)
	if err != nil {
		t.Fatal(err)
	}
	kind, decoded, err := Decode(packet)
	got, readErr := DecodeLives(decoded)
	if kind != Lives || err != nil || readErr != nil || !reflect.DeepEqual(records, got) {
		t.Fatal(got, err, readErr)
	}
	for n := 0; n < len(packet); n++ {
		if _, _, err := Decode(packet[:n]); err == nil {
			t.Fatal("truncation", n)
		}
	}
	for _, at := range []int{8, 9, 14} {
		bad := bytes.Clone(packet)
		bad[at] = 255
		if _, _, err := Decode(bad); err == nil {
			t.Fatal("invalid count/slot", at)
		}
	}
	for _, invalid := range []string{"00", "010c00000001", "010000000000", "0200000000010000000002", "01000000000100"} {
		bad, _ := hex.DecodeString(invalid)
		if _, err := DecodeLives(bad); err == nil {
			t.Fatal("invalid declaration", invalid)
		}
		if _, err := Encode(Lives, bad); err == nil {
			t.Fatal("invalid envelope", invalid)
		}
	}
	for _, invalid := range [][]Life{nil, {{Slot: 12, Generation: 1}}, {{Slot: 0, Generation: 0}}, {{Slot: 0, Generation: 1}, {Slot: 0, Generation: 2}}} {
		if _, err := EncodeLives(invalid); err == nil {
			t.Fatal("invalid declarations", invalid)
		}
	}
}
