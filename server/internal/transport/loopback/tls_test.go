package loopback

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestPrivateCertificatePersistsAndRequiresExplicitTrust(t *testing.T) {
	directory := t.TempDir()
	certificate, err := Certificate(directory)
	if err != nil {
		t.Fatal(err)
	}
	der, err := x509.ParseCertificate(certificate.Certificate[0])
	if err != nil {
		t.Fatal(err)
	}
	if err = der.VerifyHostname("127.0.0.1"); err != nil {
		t.Fatal(err)
	}
	if _, err = der.Verify(x509.VerifyOptions{Roots: x509.NewCertPool()}); err == nil {
		t.Fatal("untrusted local certificate accepted")
	}
	pem, err := os.ReadFile(filepath.Join(directory, "localhost.pem"))
	if err != nil {
		t.Fatal(err)
	}
	roots := x509.NewCertPool()
	if !roots.AppendCertsFromPEM(pem) {
		t.Fatal("invalid local CA")
	}
	if _, err = der.Verify(x509.VerifyOptions{Roots: roots, DNSName: "127.0.0.1"}); err != nil {
		t.Fatal(err)
	}
	again, err := Certificate(directory)
	if err != nil {
		t.Fatal(err)
	}
	if string(again.Certificate[0]) != string(certificate.Certificate[0]) {
		t.Fatal("local TLS identity replaced")
	}
	config := NativeTLS(certificate)
	if config.MinVersion != tls.VersionTLS10 || len(config.CipherSuites) != 2 {
		t.Fatal("native compatibility configuration")
	}
	for _, address := range []string{"0.0.0.0:0", "localhost:0", "[::1]:0"} {
		if listener, err := Listen(address); err == nil {
			listener.Close()
			t.Fatal("non-loopback bind accepted")
		}
	}
}

func TestModernTransportNegotiatesOnlyTLS12OrNewer(t *testing.T) {
	certificate, err := Certificate(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	leaf, err := x509.ParseCertificate(certificate.Certificate[0])
	if err != nil {
		t.Fatal(err)
	}
	roots := x509.NewCertPool()
	roots.AddCert(leaf)
	for _, version := range []uint16{tls.VersionTLS10, tls.VersionTLS11, tls.VersionTLS12, tls.VersionTLS13} {
		t.Run(tls.VersionName(version), func(t *testing.T) {
			a, b := net.Pipe()
			defer a.Close()
			defer b.Close()
			ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
			defer cancel()
			server := tls.Server(a, ModernTLS(certificate))
			done := make(chan error, 1)
			go func() { done <- server.HandshakeContext(ctx) }()
			client := tls.Client(b, &tls.Config{MinVersion: version, MaxVersion: version, RootCAs: roots, ServerName: "localhost"})
			clientErr := client.HandshakeContext(ctx)
			serverErr := <-done
			if version < tls.VersionTLS12 {
				if clientErr == nil || serverErr == nil {
					t.Fatal("legacy TLS accepted")
				}
				return
			}
			if clientErr != nil || serverErr != nil {
				t.Fatal("modern TLS rejected", clientErr, serverErr)
			}
			if client.ConnectionState().Version != version {
				t.Fatal("unexpected negotiated version")
			}
		})
	}
}
