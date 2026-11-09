package accounts

import (
	"bytes"
	"path/filepath"
	"testing"

	"nova3.test/backend/internal/persistence"
)

func TestCredentialVerificationAndIndependentSalts(t *testing.T) {
	s, err := persistence.Open(filepath.Join(t.TempDir(), "credentials.db"))
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	alice, err := Create(s, "alice")
	if err != nil {
		t.Fatal(err)
	}
	bob, err := Create(s, "bob")
	if err != nil {
		t.Fatal(err)
	}
	for _, account := range []Account{alice, bob} {
		if err = SetPassword(s, account.ID, "test-password"); err != nil {
			t.Fatal(err)
		}
		if id, e := Authenticate(s, account.Name, "test-password"); e != nil || id != account.ID {
			t.Fatal(id, e)
		}
	}
	for _, input := range [][2]string{{"alice", "wrong-password"}, {"unknown", "test-password"}} {
		if _, err = Authenticate(s, input[0], input[1]); err == nil {
			t.Fatal("invalid credentials accepted")
		}
	}
	var first, second []byte
	if err = s.DB.QueryRow("SELECT derived FROM credentials WHERE account=?", alice.ID).Scan(&first); err != nil {
		t.Fatal(err)
	}
	if err = s.DB.QueryRow("SELECT derived FROM credentials WHERE account=?", bob.ID).Scan(&second); err != nil {
		t.Fatal(err)
	}
	if bytes.Equal(first, second) || bytes.Contains(first, []byte("test-password")) {
		t.Fatal("password derivation/salt isolation failed")
	}
}
