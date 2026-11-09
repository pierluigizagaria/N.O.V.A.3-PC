package installation

import (
	"crypto/sha256"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"nova3.test/backend/internal/accounts"
	"nova3.test/backend/internal/catalogue"
	"nova3.test/backend/internal/persistence"
	"nova3.test/backend/internal/testfixture"
)

func prepare(t *testing.T) (string, catalogue.Catalogue) {
	t.Helper()
	root := filepath.Join(t.TempDir(), "server with spaces & symbols")
	directory := filepath.Join(root, "data")
	if err := os.MkdirAll(directory, 0700); err != nil {
		t.Fatal(err)
	}
	c, m := testfixture.NativeCatalogue()
	data, err := json.MarshalIndent(m, "", "  ")
	if err != nil {
		t.Fatal(err)
	}
	if err = os.WriteFile(filepath.Join(directory, "mapping.json"), data, 0600); err != nil {
		t.Fatal(err)
	}
	if err = os.WriteFile(filepath.Join(directory, "authority.json"), testfixture.NativeAuthority(), 0600); err != nil {
		t.Fatal(err)
	}
	db, err := persistence.Open(filepath.Join(directory, "backend.db"))
	if err != nil {
		t.Fatal(err)
	}
	if err = db.Admit(c); err != nil {
		t.Fatal(err)
	}
	if _, err = accounts.Create(db, "preserved"); err != nil {
		t.Fatal(err)
	}
	if err = db.Close(); err != nil {
		t.Fatal(err)
	}
	return root, c
}

func fileHash(t *testing.T, path string) [32]byte {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	return sha256.Sum256(data)
}

func TestRelocationIndependentOfWorkingDirectoryAndReadOnlyCheck(t *testing.T) {
	root, c := prepare(t)
	dbHash := fileHash(t, filepath.Join(root, "data", "backend.db"))
	mappingHash := fileHash(t, filepath.Join(root, "data", "mapping.json"))
	authorityHash := fileHash(t, filepath.Join(root, "data", "authority.json"))
	t.Chdir(t.TempDir())
	relocated := filepath.Join(filepath.Dir(root), "relocated server")
	if err := os.Rename(root, relocated); err != nil {
		t.Fatal(err)
	}
	installed, err := Load(filepath.Join(relocated, "nova-server.exe"))
	if err != nil {
		t.Fatal(err)
	}
	if installed.Shop.Catalogue.Version != c.Version || installed.Directory != filepath.Join(relocated, "data") {
		t.Fatal("installation was not discovered", installed)
	}
	if fileHash(t, filepath.Join(installed.Directory, "backend.db")) != dbHash || fileHash(t, filepath.Join(installed.Directory, "mapping.json")) != mappingHash {
		t.Fatal("read-only check altered data")
	}
	if fileHash(t, filepath.Join(installed.Directory, "authority.json")) != authorityHash || installed.Authority.Version() != "synthetic-v1" {
		t.Fatal("authority data not preserved")
	}
	entries, err := os.ReadDir(installed.Directory)
	if err != nil {
		t.Fatal(err)
	}
	// SQLite may create its coordination files even for a read-only WAL reader.
	// Never use immutable=1: checks must also see committed live WAL data.
	allowed := map[string]bool{"backend.db": true, "mapping.json": true, "authority.json": true, "backend.db-wal": true, "backend.db-shm": true}
	for _, entry := range entries {
		if !allowed[entry.Name()] {
			t.Fatal("check created unexpected file", entry.Name())
		}
	}
}

func TestCheckSeesCommittedWALData(t *testing.T) {
	root, c := prepare(t)
	db, err := persistence.Open(filepath.Join(root, "data", "backend.db"))
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	c.Version = "committed-in-wal"
	if err = db.Admit(c); err != nil {
		t.Fatal(err)
	}
	if _, err = Load(filepath.Join(root, "nova-server")); err == nil || !strings.Contains(err.Error(), "multiple compatible") {
		t.Fatal("check ignored committed WAL catalogue", err)
	}
}

func TestMissingDataNeverCreatesDatabase(t *testing.T) {
	root := t.TempDir()
	if _, err := Load(filepath.Join(root, "nova-server")); err == nil {
		t.Fatal("missing installation accepted")
	}
	entries, err := os.ReadDir(root)
	if err != nil || len(entries) != 0 {
		t.Fatal("check created installation files", err)
	}
	root, _ = prepare(t)
	if err = os.Remove(filepath.Join(root, "data", "backend.db")); err != nil {
		t.Fatal(err)
	}
	if _, err = Load(filepath.Join(root, "nova-server")); err == nil {
		t.Fatal("missing database accepted")
	}
	if _, err = os.Stat(filepath.Join(root, "data", "backend.db")); !os.IsNotExist(err) {
		t.Fatal("missing database was recreated", err)
	}
}

func TestIncompatibleAndAmbiguousCatalogueRejected(t *testing.T) {
	for _, scenario := range []string{"incompatible", "ambiguous", "wrong-row", "invalid-json"} {
		t.Run(scenario, func(t *testing.T) {
			root, c := prepare(t)
			dbPath := filepath.Join(root, "data", "backend.db")
			mappingPath := filepath.Join(root, "data", "mapping.json")
			switch scenario {
			case "incompatible":
				_, m := testfixture.NativeCatalogue()
				m.Resource = strings.Repeat("b", 64)
				data, _ := json.Marshal(m)
				if err := os.WriteFile(mappingPath, data, 0600); err != nil {
					t.Fatal(err)
				}
			case "ambiguous":
				db, err := persistence.Open(dbPath)
				if err != nil {
					t.Fatal(err)
				}
				c.Version = "another-compatible-version"
				if err = db.Admit(c); err != nil {
					t.Fatal(err)
				}
				db.Close()
			case "wrong-row":
				_, m := testfixture.NativeCatalogue()
				m.Rows[0].Native.TypeID++
				data, _ := json.Marshal(m)
				if err := os.WriteFile(mappingPath, data, 0600); err != nil {
					t.Fatal(err)
				}
			case "invalid-json":
				if err := os.WriteFile(mappingPath, []byte("{"), 0600); err != nil {
					t.Fatal(err)
				}
			}
			before := fileHash(t, dbPath)
			if _, err := Load(filepath.Join(root, "nova-server")); err == nil {
				t.Fatal("invalid installation accepted")
			}
			if fileHash(t, dbPath) != before {
				t.Fatal("failed check modified database")
			}
		})
	}
}
