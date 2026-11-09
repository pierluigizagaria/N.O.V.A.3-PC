package lobby

import (
	"bytes"
	"encoding/hex"
	"net/netip"
	"testing"

	"nova3.test/backend/internal/protocol/secure"
)

func TestMatchControlWireAndBounds(t *testing.T) {
	var ticket secure.Ticket
	for i := range ticket.ID {
		ticket.ID[i] = byte(i)
	}
	for i := range ticket.ClientKey {
		ticket.ClientKey[i], ticket.ServerKey[i] = byte(i), byte(i+32)
	}
	grant, err := GrantMatch(netip.MustParseAddrPort("192.168.1.2:15201"), ticket)
	if err != nil {
		t.Fatal(err)
	}
	want := "0101c0a801023b6100000001000102030405060708090a0b0c0d0e0f000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
	if grant.Opcode != MatchControlPush || hex.EncodeToString(grant.Payload) != want {
		t.Fatal("PC grant golden mismatch")
	}
	var wire bytes.Buffer
	if err = Write(&wire, grant); err != nil {
		t.Fatal(err)
	}
	read, err := Read(&wire)
	if err != nil || read.Opcode != grant.Opcode || !bytes.Equal(read.Payload, grant.Payload) {
		t.Fatal("existing lobby framing", err)
	}
	revoke := RevokeMatch(ticket.ID)
	if revoke.Opcode != MatchControlPush || hex.EncodeToString(revoke.Payload) != "0102000102030405060708090a0b0c0d0e0f" {
		t.Fatal("PC revoke golden mismatch")
	}
	for _, address := range []string{"0.0.0.0:15201", "255.255.255.255:15201", "224.1.2.3:15201", "[::1]:15201", "127.0.0.1:0"} {
		if _, err := GrantMatch(netip.MustParseAddrPort(address), ticket); err == nil {
			t.Fatal("invalid endpoint", address)
		}
	}
}
