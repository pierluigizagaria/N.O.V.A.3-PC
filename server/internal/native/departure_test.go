package native

import (
	"bytes"
	"testing"

	"nova3.test/backend/internal/protocol/lobby"
	"nova3.test/backend/internal/rooms"
)

func TestMatchDepartureUpdatesLobbyWithoutRemovingReplacement(t *testing.T) {
	service := &roomService{peers: make(map[string]*roomPeer)}
	var sent [2][]lobby.Frame
	peers := make([]*roomPeer, 2)
	for i, account := range []string{"a", "b"} {
		peers[i] = &roomPeer{player: rooms.Player{Account: account, ID: account, Name: account},
			send: func(frame lobby.Frame) bool { sent[i] = append(sent[i], frame); return true }}
		service.register(peers[i])
	}
	r, err := service.registry.Create(peers[0].player, rooms.Settings{Capacity: 2})
	if err != nil {
		t.Fatal(err)
	}
	if _, err = service.registry.Join(peers[1].player, r.ID); err != nil {
		t.Fatal(err)
	}
	lease, err := service.registry.Reserve("a")
	if err != nil || !lease.Start() {
		t.Fatal(err)
	}
	defer lease.Release()
	service.retireMatchPeer(r.ID, peers[0])
	current, err := service.registry.Current("b")
	if err != nil || current.Owner.Account != "b" || len(current.Players) != 1 || !current.Reserved() {
		t.Fatal(current, err)
	}
	if _, err = service.registry.Current("a"); err == nil {
		t.Fatal("stale membership retained")
	}
	if len(sent[1]) != 1 || sent[1][0].Opcode != lobby.MemberLeft || !bytes.Equal(sent[1][0].Payload, append(lobby.String("b"), lobby.String("a")...)) {
		t.Fatal("survivor owner/roster push", sent[1])
	}
	if len(sent[0]) != 1 || sent[0][0].Opcode != lobby.MemberRemoved {
		t.Fatal("removed client not notified")
	}
	// The callback can be delayed across a new room or lobby connection.
	other, err := service.registry.Create(peers[0].player, rooms.Settings{Capacity: 2})
	if err != nil {
		t.Fatal(err)
	}
	service.retireMatchPeer(r.ID, peers[0])
	if current, err = service.registry.Current("a"); err != nil || current.ID != other.ID {
		t.Fatal("later room removed")
	}
	replacement := *peers[0]
	service.peers["a"] = &replacement
	service.retireMatchPeer(other.ID, peers[0])
	if _, err = service.registry.Current("a"); err != nil {
		t.Fatal("replacement connection removed")
	}
}
