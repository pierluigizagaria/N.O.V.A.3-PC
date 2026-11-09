//go:build !debug

package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestReleaseRejectsDeveloperCommandsBeforeStateAccess(t *testing.T) {
	root := t.TempDir()
	t.Chdir(root)
	for _, args := range [][]string{
		{"account-create", "test"}, {"password-set", "id"}, {"tls-init"},
		{"catalogue-admit", "missing"}, {"provision", "id", "version"},
		{"reset", "id", "0"}, {"equip", "id", "missing"}, {"show", "id"},
		{"session-issue", "id"}, {"session-resolve"}, {"session-revoke"},
		{"serve", "v", "mapping"}, {"serve-native", "v", "mapping"},
		{"--native-http", "127.1.1.1:80", "serve-native", "v", "mapping"},
		{"--config", "missing", "--state", "test"},
		{"setup", "--apk", "client.apk", "--data", "client.obb"},
		{"--config", "nova-server.conf"}, {"--check-config", "nova-server.conf"},
		{"--directory", root}, {"--state", "sv06-native"}, {"--catalogue", "native-107-sv04"}, {"--mapping", "mapping.json"},
	} {
		if err := configuredRun(args, filepath.Join(root, "nova-server.exe")); err == nil || !strings.Contains(err.Error(), "unsupported option") {
			t.Fatalf("Release accepted or dispatched developer arguments %q: %v", args, err)
		}
	}
	entries, err := os.ReadDir(root)
	if err != nil || len(entries) != 0 {
		t.Fatal("rejected interface touched state", entries, err)
	}
	for _, hidden := range []string{"session-", "provision", "serve-native", "tls-init", "--native-http", "--apk", "--data", "--config", "--state", "--directory", "--catalogue", "--mapping"} {
		if strings.Contains(publicHelp, hidden) {
			t.Fatal("developer help leaked", hidden)
		}
	}
}
