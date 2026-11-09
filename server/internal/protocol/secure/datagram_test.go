package secure

import (
	"bytes"
	"encoding/binary"
	"encoding/hex"
	"math"
	"testing"
)

// These independent AES-GCM vectors are also checked against Windows CNG by
// tests/secure_datagram_tests.cpp. Only synthetic keys are used.
func TestWindowsTransportVectors(t *testing.T) {
	var ticket Ticket
	for i := range ticket.ID {
		ticket.ID[i] = byte(i)
	}
	for i := range ticket.ClientKey {
		ticket.ClientKey[i], ticket.ServerKey[i] = byte(i), byte(i+32)
	}
	plain, _ := hex.DecodeString("4e3350430101000401020304")
	for i, golden := range []string{
		"4e563301000102030405060708090a0b0c0d0e0f00000000000000015be5efbf45f5301a0f2c523df075dafc2f5cd27409096a114ee3a75b",
		"4e563301000102030405060708090a0b0c0d0e0f000000000000000145d838fc78db4fa241c496252913762e89f9fe019d6d085b71fe82fc",
	} {
		channel, err := New(ticket, i == 1)
		if err != nil {
			t.Fatal(err)
		}
		packet, err := channel.Seal(plain)
		if err != nil || hex.EncodeToString(packet) != golden {
			t.Fatal("Windows/Go ciphertext mismatch", i, err)
		}
	}
}

func TestAuthenticationDirectionReplayAndReorder(t *testing.T) {
	ticket, err := NewTicket()
	if err != nil {
		t.Fatal(err)
	}
	client, _ := New(ticket, false)
	server, _ := New(ticket, true)
	var packets [][]byte
	for i := 0; i < 70; i++ {
		p, e := client.Seal([]byte{byte(i), 1, 2, 3})
		if e != nil {
			t.Fatal(e)
		}
		packets = append(packets, p)
	}
	// Forged counters/tags cannot poison the receiver's replay window.
	for _, mutate := range []func([]byte){
		func(p []byte) { p[4] ^= 1 }, func(p []byte) { binary.BigEndian.PutUint64(p[20:], math.MaxUint64) }, func(p []byte) { p[len(p)-1] ^= 1 },
	} {
		bad := bytes.Clone(packets[0])
		mutate(bad)
		if _, err := server.Open(bad); err == nil || server.highest != 0 {
			t.Fatal("forgery mutated replay window", err)
		}
	}
	for _, i := range []int{2, 0, 1, 69, 68, 10} {
		plain, err := server.Open(packets[i])
		if err != nil || !bytes.Equal(plain, []byte{byte(i), 1, 2, 3}) {
			t.Fatal(i, plain, err)
		}
		if _, err := server.Open(packets[i]); err == nil {
			t.Fatal("accepted replay")
		}
	}
	if _, err := server.Open(packets[3]); err == nil {
		t.Fatal("accepted outside replay window")
	}
	if _, err := client.Open(packets[69]); err == nil {
		t.Fatal("accepted reflected client packet")
	}
	reply, _ := server.Seal([]byte("unchanged native reply"))
	if plain, err := client.Open(reply); err != nil || string(plain) != "unchanged native reply" {
		t.Fatal(err)
	}
	other, _ := NewTicket()
	wrong, _ := New(other, true)
	if _, err := wrong.Open(packets[0]); err == nil {
		t.Fatal("cross-ticket packet")
	}
}

func TestEnvelopeBoundsAndNonceExhaustion(t *testing.T) {
	ticket, _ := NewTicket()
	client, _ := New(ticket, false)
	server, _ := New(ticket, true)
	p, _ := client.Seal(bytes.Repeat([]byte{1}, MaxPayload))
	if _, err := server.Open(p); err != nil {
		t.Fatal(err)
	}
	for i := 0; i < HeaderSize+17; i++ {
		if _, err := server.Open(p[:i]); err == nil {
			t.Fatal("truncated", i)
		}
	}
	for _, body := range [][]byte{nil, make([]byte, MaxPayload+1)} {
		if _, err := client.Seal(body); err == nil {
			t.Fatal("invalid size")
		}
	}
	client.next = math.MaxUint64
	if _, err := client.Seal([]byte{1}); err == nil {
		t.Fatal("nonce wrap")
	}
}
