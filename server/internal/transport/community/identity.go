// Package community owns domain identity for an independently deployed server.
package community

import (
	"crypto/tls"
	"crypto/x509"
	"errors"
	"fmt"
	"net"
	"net/netip"
	"path/filepath"
	"strings"
	"time"
)

func ValidAddress(value string) bool {
	ip, err := netip.ParseAddr(value)
	return err == nil && ip.Is4() && !ip.IsUnspecified() && !ip.IsMulticast() && value != "255.255.255.255"
}

func ValidDomain(domain string) bool {
	if domain == "" || len(domain) > 253 || net.ParseIP(domain) != nil || strings.Trim(domain, "0123456789.") == "" {
		return false
	}
	for _, label := range strings.Split(domain, ".") {
		if len(label) == 0 || len(label) > 63 || label[0] == '-' || label[len(label)-1] == '-' {
			return false
		}
		for _, c := range label {
			if !(c >= 'a' && c <= 'z' || c >= '0' && c <= '9' || c == '-') {
				return false
			}
		}
	}
	return true
}

// Identity reads the conventional ACME chain/key from the installation-owned
// certificate directory. Public startup never creates a self-signed fallback.
// Certificate renewal replaces these two files and takes effect on restart.
func Identity(directory, domain string, now time.Time) (tls.Certificate, error) {
	if !ValidDomain(domain) {
		return tls.Certificate{}, errors.New("domain must be a lowercase DNS hostname without a scheme, port or path")
	}
	certificate, err := tls.LoadX509KeyPair(filepath.Join(directory, "fullchain.pem"), filepath.Join(directory, "privkey.pem"))
	if err != nil {
		return tls.Certificate{}, fmt.Errorf("cannot load data/certificates/fullchain.pem and privkey.pem: %w", err)
	}
	leaf, err := x509.ParseCertificate(certificate.Certificate[0])
	if err != nil {
		return tls.Certificate{}, err
	}
	if err = leaf.VerifyHostname(domain); err != nil {
		return tls.Certificate{}, fmt.Errorf("certificate does not cover the configured domain: %w", err)
	}
	if now.Before(leaf.NotBefore) || !now.Before(leaf.NotAfter) {
		return tls.Certificate{}, errors.New("domain certificate is not currently valid")
	}
	if leaf.IsCA {
		return tls.Certificate{}, errors.New("domain identity must be a server certificate, not a CA")
	}
	serverUsage := len(leaf.ExtKeyUsage) == 0 && len(leaf.UnknownExtKeyUsage) == 0
	for _, usage := range leaf.ExtKeyUsage {
		serverUsage = serverUsage || usage == x509.ExtKeyUsageServerAuth || usage == x509.ExtKeyUsageAny
	}
	if !serverUsage {
		return tls.Certificate{}, errors.New("domain certificate does not permit TLS server authentication")
	}
	certificate.Leaf = leaf
	return certificate, nil
}
