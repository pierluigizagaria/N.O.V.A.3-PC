package legacy

import (
	"bytes"
	"testing"
)

func TestOriginalCodecDirectionAndCanonicalTermination(t *testing.T) {
	// Invented R04 vector independently checked against the original encoder.
	plain := []byte("f|1|i|7|v|1.0.7|")
	if Encode(plain) != "MXxm8LgF3WND8fJlW4Yn8b" {
		t.Fatal("original low-bit-first alphabet/order changed")
	}
	for _, data := range [][]byte{plain, {1}, {1, 2}, {1, 2, 3}, {0, 0, 0}, bytes.Repeat([]byte{255}, 255)} {
		decoded, err := Decode(Encode(data))
		if err != nil || !bytes.Equal(decoded, data) {
			t.Fatalf("round-trip length %d: %v", len(data), err)
		}
	}
	for _, invalid := range []string{"", "!", "abc=", "aaaaa-"} {
		if _, err := Decode(invalid); err == nil {
			t.Fatalf("accepted %q", invalid)
		}
	}
}

func TestNativeEnvelopeAndAmbiguousRequestRejection(t *testing.T) {
	extended, err := Parse([]byte("g|112|i|7|u|invented|"))
	if err != nil || extended.Function != 612 || extended.Success("0|") != "g|112|r|s|0|" {
		t.Fatal("extended native namespace", extended, err)
	}
	r, err := Parse([]byte("f|15|i|7|u|tester|p|private-test|"))
	if err != nil || r.Function != 15 || r.Fields["u"] != "tester" {
		t.Fatal(r, err)
	}
	for _, data := range []string{"f|1|i|7|i|8|", "f|1|dangling", "x|1|", "f|0|", "f|1|i|a\x00b|"} {
		if _, err := Parse([]byte(data)); err == nil {
			t.Fatalf("accepted %q", data)
		}
	}
	if Success(97, "") != "f|97|r|s|" || Failure(15, 40) != "f|15|r|e|40|" {
		t.Fatal("native envelope changed")
	}
}
