package main

import (
	"crypto/sha256"
	"crypto/x509"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"fmt"
	"net/http"
	"net/netip"
	"net/url"
	"slices"
	"strings"
)

var ErrUntrustedProxy = errors.New("client certificate headers from an untrusted address")

// ParseTrustedProxies parses the -trusted-proxy list (comma separated). Only loopback addresses are
// allowed: the proxy (nginx) runs on the same host, and a LAN address must never be able to assert an
// agent identity (DECISIONS #24).
func ParseTrustedProxies(list string) ([]netip.Addr, error) {
	var out []netip.Addr
	for _, part := range strings.Split(list, ",") {
		part = strings.TrimSpace(part)
		if part == "" {
			continue
		}
		addr, err := netip.ParseAddr(part)
		if err != nil {
			return nil, fmt.Errorf("trusted proxy %q: %w", part, err)
		}
		if !addr.Unmap().IsLoopback() {
			return nil, fmt.Errorf("trusted proxy %q is not a loopback address", part)
		}
		out = append(out, addr.Unmap())
	}
	return out, nil
}

// AgentCertFingerprint returns the agent identity: the lowercase hex SHA-256 of the client
// certificate (DER), the same value the server stores as agents.cert_fingerprint.
// The certificate comes from the TLS connection, or from nginx's X-SSL-Client-Verify /
// X-SSL-Client-Cert headers when the request comes from a trusted loopback proxy.
// X-Agent-Cert-Fingerprint is never trusted.
func AgentCertFingerprint(r *http.Request, trusted []netip.Addr) (string, error) {
	if r.TLS != nil && len(r.TLS.PeerCertificates) > 0 {
		h := sha256.Sum256(r.TLS.PeerCertificates[0].Raw)
		return hex.EncodeToString(h[:]), nil
	}

	escaped := r.Header.Get("X-SSL-Client-Cert")
	if escaped == "" {
		return "", ErrMissingCert
	}
	ap, err := netip.ParseAddrPort(r.RemoteAddr)
	if err != nil || !slices.Contains(trusted, ap.Addr().Unmap()) {
		return "", ErrUntrustedProxy
	}
	if r.Header.Get("X-SSL-Client-Verify") != "SUCCESS" {
		return "", ErrMissingCert
	}
	// nginx $ssl_client_escaped_cert is percent-encoded; '+' is a literal base64 character.
	pemText, err := url.PathUnescape(escaped)
	if err != nil {
		return "", ErrMissingCert
	}
	block, _ := pem.Decode([]byte(pemText))
	if block == nil || block.Type != "CERTIFICATE" {
		return "", ErrMissingCert
	}
	if _, err := x509.ParseCertificate(block.Bytes); err != nil {
		return "", ErrMissingCert
	}
	h := sha256.Sum256(block.Bytes)
	return hex.EncodeToString(h[:]), nil
}
