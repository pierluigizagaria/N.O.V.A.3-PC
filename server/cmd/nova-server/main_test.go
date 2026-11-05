package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestRejectInvalidAdministrativeInvocation(t *testing.T) {
	for _, args := range [][]string{{}, {"unknown"}, {"account-create"}, {"--state", "../player", "account-create", "alice"}, {"show", "one", "two"}} {
		if err := run(args); err == nil {
			t.Fatalf("accepted invalid arguments: %v", args)
		}
	}
}

func TestWorkspaceWithoutPrivateDocumentation(t *testing.T) {
	root := t.TempDir()
	nested := filepath.Join(root, "server", "cmd")
	if err := os.MkdirAll(nested, 0700); err != nil {
		t.Fatal(err)
	}
	for _, path := range []string{"CMakeLists.txt", "server/go.mod"} {
		if err := os.WriteFile(filepath.Join(root, path), nil, 0600); err != nil {
			t.Fatal(err)
		}
	}
	t.Chdir(nested)
	got, err := workspace()
	if err != nil || got != root {
		t.Fatalf("workspace() = %q, %v; want %q", got, err, root)
	}
}
