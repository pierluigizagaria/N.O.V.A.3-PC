package accounts

import (
	"nova3.test/backend/internal/persistence"
	"path/filepath"
	"testing"
)

func TestLobbyIdentityStableDistinctAndBounded(t *testing.T) {
	path := filepath.Join(t.TempDir(), "identities.db")
	s, err := persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	a, err := Create(s, "same-prefix-123456-alice")
	if err != nil {
		t.Fatal(err)
	}
	b, err := Create(s, "same-prefix-123456-bob")
	if err != nil {
		t.Fatal(err)
	}
	x, n, err := LobbyIdentity(s, a.ID)
	if err != nil || len(x) > 16 || len(n) > 16 {
		t.Fatal(x, n, err)
	}
	y, _, err := LobbyIdentity(s, b.ID)
	if err != nil || x == y {
		t.Fatal("identities collide", err)
	}
	if _, _, err = LobbyIdentity(s, "missing"); err == nil {
		t.Fatal("unknown account admitted")
	}
	if err = s.Close(); err != nil {
		t.Fatal(err)
	}
	s, err = persistence.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	if next, _, err := LobbyIdentity(s, a.ID); err != nil || next != x {
		t.Fatal("identity changed on restart", err)
	}
}
