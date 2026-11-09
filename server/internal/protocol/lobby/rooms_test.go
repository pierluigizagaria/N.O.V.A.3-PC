package lobby

import (
	"bytes"
	"encoding/hex"
	"strings"
	"testing"
)

func TestNativeNameAndFriendSearch(t *testing.T) {
	name, _ := hex.DecodeString("0000000000000000000005616c696365")
	if got, err := ParseFindRoom(name); err != nil || got != "alice" {
		t.Fatal(got, err)
	}
	friends, _ := hex.DecodeString("00000017020000000001020005616c6963650003626f620001")
	v, err := ParseFriendRooms(friends)
	if err != nil || v.UserNumber != 23 || v.Type != 1 || v.Flags != 1 || strings.Join(v.Names, ",") != "alice,bob" {
		t.Fatal(v, err)
	}
	for i := range friends {
		if _, err := ParseFriendRooms(friends[:i]); err == nil {
			t.Fatalf("truncation %d", i)
		}
	}
	for _, data := range [][]byte{append(append([]byte{}, friends...), 0), append([]byte{}, friends...)} {
		data[10] = 31
		if _, err := ParseFriendRooms(data); err == nil {
			t.Fatal("bad friend count accepted")
		}
	}
	r := RoomRecord{ID: 7, Name: "p1", Owner: "A", GameData: "g", Version: 107}
	v1, err := RoomListVersion([]RoomRecord{r, r}, FindRoomV1Response, 1)
	if err != nil {
		t.Fatal(err)
	}
	v2, err := RoomList([]RoomRecord{r, r})
	if err != nil || len(v2.Payload)-len(v1.Payload) != 8 || v1.Opcode != 98 {
		t.Fatal("version 1 must omit every version word")
	}
}

func TestRoomWireRecordsAndBounds(t *testing.T) {
	// Native sendGetLobbyListPackage writes login "un", count,
	// offset, one tournament equality, room type, extended-info flag (17 bytes).
	b, _ := hex.DecodeString("0000000132000000000100020001300101")
	v, e := ParseListRooms(b)
	if e != nil || v.UserNumber != 1 || v.Limit != 50 || v.Offset != 0 || v.Type != 1 || v.Flags != 1 || len(v.Parameters) != 1 || v.Parameters[0] != (Parameter{0, 2, "0"}) {
		t.Fatal(v, e)
	}
	for i := 0; i < len(b); i++ {
		if _, e = ParseListRooms(b[:i]); e == nil {
			t.Fatalf("truncated query %d", i)
		}
	}
	if _, e = ParseListRooms(append(b, 0)); e == nil {
		t.Fatal("trailing query")
	}
	// Golden list and info records deliberately use different field orders.
	r := RoomRecord{ID: 7, Name: "p1", Owner: "A", Capacity: 2, Started: 1, Type: 1, Flags: 0, PlayerCount: 9, Parameter2: 10, GameData: "g", Version: 107}
	list, e := RoomList([]RoomRecord{r})
	if e != nil {
		t.Fatal(e)
	}
	golden, _ := hex.DecodeString("0000000000000001000000070002703100014102010100000000090000000a0001670000006b")
	if list.Opcode != 129 || !bytes.Equal(list.Payload, golden) {
		t.Fatalf("list %x", list.Payload)
	}
	info, e := RoomInfo(r)
	golden, _ = hex.DecodeString("00000000000270310001670100000000090000000a00014102010000006b00000000")
	if e != nil || info.Opcode != 23 || !bytes.Equal(info.Payload, golden) {
		t.Fatalf("info %x", info.Payload)
	}
	joined, e := JoinedRoom([]PlayerRecord{{"p1", "A", 3, "x"}, {"p2", "B", 3, "y"}})
	golden, _ = hex.DecodeString("000000000000000200027031000141030001780002703200014203000179")
	if e != nil || joined.Opcode != 16 || !bytes.Equal(joined.Payload, golden) {
		t.Fatalf("join %x %v", joined.Payload, e)
	}
	if _, e = RoomList(make([]RoomRecord, 256)); e == nil {
		t.Fatal("oversized list")
	}
	if _, e = PlayerBytes(PlayerRecord{ID: strings.Repeat("p", 17), Name: "A"}); e == nil {
		t.Fatal("long player ID")
	}
}

func TestWaitingRoomRejoinWire(t *testing.T) {
	r := RoomRecord{Name: "p1", Type: 1, Capacity: 2, GameData: "g"}
	players := []PlayerRecord{{"p1", "A", 3, "x"}, {"p2", "B", 3, "y"}}
	frame, err := RejoinedRoom(r, players)
	golden, _ := hex.DecodeString("0000000000027031010002000167000000000000000000000200027031000141030001780002703200014203000179")
	if err != nil || frame.Opcode != RejoinRoomResponse || !bytes.Equal(frame.Payload, golden) {
		t.Fatalf("rejoin %x %v", frame.Payload, err)
	}
	r.Started = 1
	if _, err := RejoinedRoom(r, players); err == nil {
		t.Fatal("running room requires a new transport admission")
	}
}

func TestRoomCreateAndUpdateRejectMalformedPayloads(t *testing.T) {
	game := "0|1|2|3|4|5|6|7|8|9|10|room"
	b := append(String("p1"), 1, 0, 0, 2)
	b = append(b, String(game)...)
	b = append(b, String("player")...)
	b = append(b, 1, 0, 0, 0, 1, '0')
	b = append(b, Integers(107)...)
	v, e := ParseCreateRoom(b)
	if e != nil || v.Capacity != 2 || v.Version != 107 || v.GameData != game || !ValidGameData(v.GameData) {
		t.Fatal(v, e)
	}
	for i := 0; i < len(b); i++ {
		if _, e = ParseCreateRoom(b[:i]); e == nil {
			t.Fatalf("truncated create %d", i)
		}
	}
	for _, bad := range []string{"", game + "|extra", strings.Replace(game, "|room", "|"+strings.Repeat("x", 40), 1), strings.Replace(game, "0|", "2147483648|", 1)} {
		if ValidGameData(bad) {
			t.Fatal("unsafe native game data", bad)
		}
	}
	if _, e = ParseCreateRoom(append(b, 0)); e == nil {
		t.Fatal("trailing create")
	}
	if _, e = ParsePlayerData([]byte{0xff, 0xff}); e == nil {
		t.Fatal("signed blob length")
	}
	if _, e = ParsePlayerData(String("a\x00b")); e == nil {
		t.Fatal("embedded nul")
	}
	if _, _, e = ParseGameData(append(String(game), 2, 0, 0, 0, 1, '1', 0, 0, 0, 1, '2')); e == nil {
		t.Fatal("duplicate properties")
	}
}
