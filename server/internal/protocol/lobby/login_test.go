package lobby

import (
	"encoding/binary"
	"testing"
)

func TestLoginFieldsAndMalformedLengths(t *testing.T) {
	payload := make([]byte, 4)
	binary.BigEndian.PutUint32(payload, 7)
	payload = append(payload, String("alice")...)
	payload = append(payload, 0, 0, 0, 0, 1)
	value, err := ParseLogin(payload)
	if err != nil || value.GGI != 7 || value.UID != "alice" || value.Mode != 0 || value.Version != 1 {
		t.Fatal(value, err)
	}
	for _, invalid := range [][]byte{nil, payload[:10], payload[:len(payload)-1], append(append([]byte{}, payload...), 0), make([]byte, 11)} {
		if _, err := ParseLogin(invalid); err == nil {
			t.Fatal("invalid login accepted")
		}
	}
	response, err := LoginAccepted("p1", "alice")
	if err != nil || response.Opcode != LoginResponse || string(response.Payload) != "\x00\x00\x00\x00\x00\x02p1\x00\x05alice" {
		t.Fatal("wire response changed")
	}
}
