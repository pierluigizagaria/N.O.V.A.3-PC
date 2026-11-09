package community

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"encoding/pem"
	"math/big"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestDomainIdentityValidationAndNoFallback(t *testing.T) {
	now := time.Now()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	encoded, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		t.Fatal(err)
	}
	for _, test := range []struct {
		name     string
		alter    func(*x509.Certificate)
		accepted bool
	}{
		{"valid", func(*x509.Certificate) {}, true},
		{"wildcard", func(c *x509.Certificate) { c.DNSNames = []string{"*.example.org"} }, true},
		{"wrong-domain", func(c *x509.Certificate) { c.DNSNames = []string{"other.example.org"} }, false},
		{"expired", func(c *x509.Certificate) { c.NotAfter = now.Add(-time.Minute) }, false},
		{"future", func(c *x509.Certificate) { c.NotBefore = now.Add(time.Minute) }, false},
		{"ca", func(c *x509.Certificate) { c.IsCA = true }, false},
		{"client-only", func(c *x509.Certificate) { c.ExtKeyUsage = []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth} }, false},
	} {
		t.Run(test.name, func(t *testing.T) {
			dir := t.TempDir()
			leaf := &x509.Certificate{SerialNumber: big.NewInt(1), DNSNames: []string{"game.example.org"}, NotBefore: now.Add(-time.Hour), NotAfter: now.Add(time.Hour), BasicConstraintsValid: true, KeyUsage: x509.KeyUsageDigitalSignature, ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}}
			test.alter(leaf)
			der, err := x509.CreateCertificate(rand.Reader, leaf, leaf, &key.PublicKey, key)
			if err != nil {
				t.Fatal(err)
			}
			for name, body := range map[string][]byte{"fullchain.pem": pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}), "privkey.pem": pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: encoded})} {
				if err = os.WriteFile(filepath.Join(dir, name), body, 0600); err != nil {
					t.Fatal(err)
				}
			}
			_, err = Identity(dir, "game.example.org", now)
			if (err == nil) != test.accepted {
				t.Fatalf("accepted=%v error=%v", test.accepted, err)
			}
		})
	}
	dir := t.TempDir()
	if _, err := Identity(dir, "game.example.org", now); err == nil {
		t.Fatal("missing public certificate accepted")
	}
	entries, _ := os.ReadDir(dir)
	if len(entries) != 0 {
		t.Fatal("public mode generated fallback identity")
	}
}

func TestDomainSyntax(t *testing.T) {
	for _, domain := range []string{"game.example.org", "lan-server", "a.b", "localhost"} {
		if !ValidDomain(domain) {
			t.Fatal(domain)
		}
	}
	for _, domain := range []string{"", "https://game.example.org", "game.example.org:443", "game.example.org/", "Game.example.org", "127.0.0.1", "-bad.example", "bad-.example", "a..b", "a.b.", "*.example.org", strings.Repeat("a", 64) + ".org"} {
		if ValidDomain(domain) {
			t.Fatal(domain)
		}
	}
}
