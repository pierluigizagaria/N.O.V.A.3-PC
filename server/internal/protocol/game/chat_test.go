package game

import (
	"bytes"
	"testing"
)

func TestChatOriginalBuilderAndAllowlist(t *testing.T) {
	value := Chat{Team: 0, Actor: 11, MessageID: 0x01020304}
	wire, err := value.Message()
	if err != nil || !bytes.Equal(wire, []byte{130, 0, 11, 1, 2, 3, 4}) {
		t.Fatal(wire, err)
	}
	if got, err := ReadChat(wire); err != nil || got != value {
		t.Fatal(got, err)
	}
	if value.GlobalAllowed() {
		t.Fatal("arbitrary localized string admitted")
	}
	for id := uint32(1379); id <= 1385; id++ {
		if (Chat{MessageID: id}).GlobalAllowed() != (id >= 1380 && id <= 1384) {
			t.Fatal(id)
		}
	}
	if (Chat{Team: 1, MessageID: 1380}).GlobalAllowed() {
		t.Fatal("team intent became global")
	}
	for _, b := range [][]byte{nil, {130}, {130, 0, 12, 0, 0, 0, 0}, {130, 3, 0, 0, 0, 0, 0}, {129, 0, 0, 0, 0, 0, 0}} {
		if _, err := ReadChat(b); err == nil {
			t.Fatal("invalid chat", b)
		}
	}
}
