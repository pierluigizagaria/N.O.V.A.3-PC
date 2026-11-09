package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/testfixture"
)

func TestProxyDomainRejectsInvalidIdentityBeforeLoadingData(t *testing.T) {
	for _, domain := range []string{"", "https://game.example.org", "127.0.0.1", "game.example.org:443", "UPPER.example"} {
		err := configuredRun([]string{"--proxy-domain", domain}, filepath.Join(t.TempDir(), "absent.exe"))
		if err == nil || !strings.Contains(err.Error(), "domain must") {
			t.Fatalf("invalid proxy domain %q: %v", domain, err)
		}
	}
}

func TestInstalledCheckNeedsNoConfiguration(t *testing.T) {
	root := t.TempDir()
	directory := filepath.Join(root, "data")
	if err := os.Mkdir(directory, 0700); err != nil {
		t.Fatal(err)
	}
	c, m := testfixture.NativeCatalogue()
	data, _ := json.Marshal(m)
	if err := os.WriteFile(filepath.Join(directory, "mapping.json"), data, 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(directory, "authority.json"), testfixture.NativeAuthority(), 0600); err != nil {
		t.Fatal(err)
	}
	db, err := persistence.Open(filepath.Join(directory, "backend.db"))
	if err != nil {
		t.Fatal(err)
	}
	if err = db.Admit(c); err != nil {
		t.Fatal(err)
	}
	db.Close()
	t.Chdir(t.TempDir())
	if err = configuredRun([]string{"--check"}, filepath.Join(root, "nova-server.exe")); err != nil {
		t.Fatal(err)
	}
	if _, err = os.Stat(filepath.Join(root, "nova-server.conf")); !os.IsNotExist(err) {
		t.Fatal("check created configuration", err)
	}
}
