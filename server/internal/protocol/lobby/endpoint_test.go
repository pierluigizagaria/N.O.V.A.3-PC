package lobby

import (
	"bytes"
	"encoding/hex"
	"strings"
	"testing"
)

func TestGameEndpointOriginalWire(t *testing.T) {
	f, err := AnnounceGameEndpoint(GameEndpoint{"127.1.2.3", 12345, 54321, 7})
	if err != nil {
		t.Fatal(err)
	}
	// Consumed independently by native_room_codec_tests.cpp. Distinct ports,
	// including one above INT16_MAX, detect swapped fields and signed narrowing.
	want, _ := hex.DecodeString("00093132372e312e322e333039d43100000007")
	if f.Opcode != 50 || !bytes.Equal(f.Payload, want) {
		t.Fatalf("endpoint opcode=%d payload=%x", f.Opcode, f.Payload)
	}
	var wire bytes.Buffer
	if err := Write(&wire, f); err != nil {
		t.Fatal(err)
	}
	want, _ = hex.DecodeString("001901020000003200093132372e312e322e333039d43100000007")
	if !bytes.Equal(wire.Bytes(), want) {
		t.Fatalf("endpoint frame=%x", wire.Bytes())
	}
}

func TestGameEndpointRejectsInvalidDestinations(t *testing.T) {
	for _, address := range []string{"", "0.0.0.0", "224.1.2.3", "255.255.255.255", "::1", "::ffff:127.1.2.3", "127.1.2.3:7891", "127.1.2.3\x00extra", "127.1.2.3\n", "host.invalid", strings.Repeat("a", 128)} {
		t.Run(address, func(t *testing.T) {
			f, err := AnnounceGameEndpoint(GameEndpoint{address, 12345, 54321, 7})
			if err == nil || len(f.Payload) != 0 {
				t.Fatal("invalid endpoint produced an announcement")
			}
		})
	}
	for _, v := range []GameEndpoint{{"127.1.2.3", 0, 54321, 7}, {"127.1.2.3", 12345, 0, 7}, {"127.1.2.3", 12345, 54321, 0}} {
		if _, err := AnnounceGameEndpoint(v); err == nil {
			t.Fatal("incomplete endpoint accepted", v)
		}
	}
}
