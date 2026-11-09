package game

import (
	"bytes"
	"encoding/hex"
	"testing"
)

func TestOriginalRespawnMessages(t *testing.T) {
	r := Respawn{Time: 117, Slot: 0, Spawn: 0x01020304}
	body, err := r.Message()
	if err != nil || hex.EncodeToString(body) != "1f000000750001020304" {
		t.Fatal(body, err)
	}
	if got, err := ReadRespawn(body); err != nil || got != r {
		t.Fatal(got, err)
	}
	request, err := RespawnRequest(0)
	if err != nil || hex.EncodeToString(request) != "2000" {
		t.Fatal(request, err)
	}
	for _, msg := range [][]byte{body, request} {
		packet, err := Encode(Header{Reliable: true}, msg)
		if err != nil {
			t.Fatal(err)
		}
		for n := HeaderSize; n < len(packet); n++ {
			if _, err := Decode(packet[:n]); err == nil {
				t.Fatal("truncation", n)
			}
		}
		decoded, err := Decode(packet)
		if err != nil || !bytes.Equal(decoded.Messages[0], msg) {
			t.Fatal(decoded, err)
		}
	}
	for _, bad := range [][]byte{{RequestRespawn, 12}, {RespawnPlayer, 0, 0, 0, 0, 12, 0, 0, 0, 0}, {RespawnPlayer, 0, 0, 0, 0, 0, 255, 255, 255, 255}} {
		if _, err := Encode(Header{}, bad); err == nil {
			t.Fatal("invalid respawn")
		}
	}
	if _, err := RespawnRequest(12); err == nil {
		t.Fatal("invalid slot")
	}
	if _, err := (Respawn{Spawn: 0xffffffff}).Message(); err == nil {
		t.Fatal("invalid spawn")
	}
}
