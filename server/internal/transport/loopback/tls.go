// Package loopback owns development listeners and private local TLS identity.
package loopback

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"errors"
	"math/big"
	"net"
	"os"
	"path/filepath"
	"time"
)

func Listen(address string) (net.Listener, error) {
	host, _, err := net.SplitHostPort(address)
	ip := net.ParseIP(host)
	if err != nil || ip == nil || ip.To4() == nil || !ip.IsLoopback() {
		return nil, errors.New("native services require literal IPv4 loopback")
	}
	return net.Listen("tcp4", address)
}

// Certificate stays private to this backend state. No OS trust store is modified.
func Certificate(directory string) (tls.Certificate, error) {
	certPath, keyPath := filepath.Join(directory, "localhost.pem"), filepath.Join(directory, "localhost-key.pem")
	_, certErr := os.Stat(certPath)
	_, keyErr := os.Stat(keyPath)
	if certErr == nil || keyErr == nil {
		return tls.LoadX509KeyPair(certPath, keyPath)
	}
	if !os.IsNotExist(certErr) || !os.IsNotExist(keyErr) {
		return tls.Certificate{}, errors.New("cannot inspect local TLS identity")
	}
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return tls.Certificate{}, err
	}
	serial, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		return tls.Certificate{}, err
	}
	now := time.Now()
	certificate := &x509.Certificate{SerialNumber: serial, Subject: pkix.Name{CommonName: "NOVA localhost test service"}, NotBefore: now.Add(-time.Hour), NotAfter: now.AddDate(1, 0, 0), KeyUsage: x509.KeyUsageCertSign | x509.KeyUsageDigitalSignature | x509.KeyUsageKeyEncipherment, ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}, BasicConstraintsValid: true, IsCA: true, IPAddresses: []net.IP{net.ParseIP("127.0.0.1"), net.ParseIP("127.1.1.1")}, DNSNames: []string{"localhost"}}
	der, err := x509.CreateCertificate(rand.Reader, certificate, certificate, &key.PublicKey, key)
	if err != nil {
		return tls.Certificate{}, err
	}
	certPEM := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der})
	keyPEM := pem.EncodeToMemory(&pem.Block{Type: "RSA PRIVATE KEY", Bytes: x509.MarshalPKCS1PrivateKey(key)})
	if err = os.WriteFile(keyPath, keyPEM, 0600); err != nil {
		return tls.Certificate{}, err
	}
	if err = os.WriteFile(certPath, certPEM, 0600); err != nil {
		return tls.Certificate{}, err
	}
	return tls.X509KeyPair(certPEM, keyPEM)
}

func NativeTLS(certificate tls.Certificate) *tls.Config {
	// The original bundled OpenSSL speaks TLS1.0/RSA. This compatibility is scoped
	// to explicit loopback listeners; certificate verification remains enabled.
	return &tls.Config{Certificates: []tls.Certificate{certificate}, MinVersion: tls.VersionTLS10, MaxVersion: tls.VersionTLS12, CipherSuites: []uint16{tls.TLS_RSA_WITH_AES_128_CBC_SHA, tls.TLS_RSA_WITH_AES_256_CBC_SHA}, NextProtos: []string{"http/1.1"}}
}

// ModernTLS is used by the PC transport, including the original login socket
// through its Windows TLS adapter. NativeTLS remains a private original-code
// compatibility fixture; production no longer negotiates TLS 1.0/RSA suites.
func ModernTLS(certificate tls.Certificate) *tls.Config {
	return &tls.Config{Certificates: []tls.Certificate{certificate}, MinVersion: tls.VersionTLS12, NextProtos: []string{"http/1.1"}}
}
