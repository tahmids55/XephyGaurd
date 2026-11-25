package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"math/big"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"net/url"
	"strings"
	"testing"
	"time"
)

func testCertDER(t *testing.T) []byte {
	t.Helper()
	priv, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	tmpl := x509.Certificate{
		SerialNumber: big.NewInt(7),
		Subject:      pkix.Name{CommonName: "agent"},
		NotBefore:    time.Now().Add(-time.Hour),
		NotAfter:     time.Now().Add(time.Hour),
	}
	der, err := x509.CreateCertificate(rand.Reader, &tmpl, &tmpl, &priv.PublicKey, priv)
	if err != nil {
		t.Fatal(err)
	}
	return der
}

// nginxEscaped mimics $ssl_client_escaped_cert: percent-encoded PEM with a space as %20 and base64
// '+' as %2B (seen from nginx: "-----BEGIN%20CERTIFICATE-----%0A...%2F...").
func nginxEscaped(der []byte) string {
	q := url.QueryEscape(string(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der})))
	return strings.ReplaceAll(q, "+", "%20")
}

func proxiedRequest(remote, verify, cert string) *http.Request {
	req := httptest.NewRequest("GET", "/screen-gw/agent", nil)
	req.RemoteAddr = remote
	if verify != "" {
		req.Header.Set("X-SSL-Client-Verify", verify)
	}
	if cert != "" {
		req.Header.Set("X-SSL-Client-Cert", cert)
	}
	return req
}

var loopbackOnly = []netip.Addr{netip.MustParseAddr("127.0.0.1")}

func TestAgentCertFromTrustedProxyIsSHA256OfDER(t *testing.T) {
	der := testCertDER(t)
	want := sha256.Sum256(der)
	escaped := nginxEscaped(der)
	if !strings.HasPrefix(escaped, "-----BEGIN%20CERTIFICATE-----%0A") {
		t.Fatalf("escape does not match nginx: %s", escaped[:40])
	}
	fp, err := AgentCertFingerprint(proxiedRequest("127.0.0.1:40000", "SUCCESS", escaped), loopbackOnly)
	if err != nil {
		t.Fatalf("expected success, got %v", err)
	}
	if fp != hex.EncodeToString(want[:]) {
		t.Fatalf("fingerprint %s, want %x", fp, want)
	}
}

func TestAgentCertRejectsUntrustedOrUnverified(t *testing.T) {
	escaped := nginxEscaped(testCertDER(t))
	cases := []struct {
		name    string
		req     *http.Request
		trusted []netip.Addr
		want    error
	}{
		{"no proxy configured", proxiedRequest("127.0.0.1:1", "SUCCESS", escaped), nil, ErrUntrustedProxy},
		{"LAN address", proxiedRequest("192.168.10.5:1", "SUCCESS", escaped), loopbackOnly, ErrUntrustedProxy},
		{"other loopback", proxiedRequest("127.0.0.2:1", "SUCCESS", escaped), loopbackOnly, ErrUntrustedProxy},
		{"verify failed", proxiedRequest("127.0.0.1:1", "FAILED:unknown ca", escaped), loopbackOnly, ErrMissingCert},
		{"verify missing", proxiedRequest("127.0.0.1:1", "", escaped), loopbackOnly, ErrMissingCert},
		{"not a cert", proxiedRequest("127.0.0.1:1", "SUCCESS", url.QueryEscape("garbage")), loopbackOnly, ErrMissingCert},
		{"bad escape", proxiedRequest("127.0.0.1:1", "SUCCESS", "%zz"), loopbackOnly, ErrMissingCert},
		{"no cert header", proxiedRequest("127.0.0.1:1", "SUCCESS", ""), loopbackOnly, ErrMissingCert},
	}
	for _, c := range cases {
		if _, err := AgentCertFingerprint(c.req, c.trusted); !errors.Is(err, c.want) {
			t.Errorf("%s: got %v, want %v", c.name, err, c.want)
		}
	}
}

func TestAgentFingerprintHeaderIsNeverTrusted(t *testing.T) {
	g := NewGatewayAuthenticator("secret", "iss", "aud", false, loopbackOnly, staticPCs{})
	req := proxiedRequest("127.0.0.1:1", "SUCCESS", "")
	req.Header.Set("X-Agent-Cert-Fingerprint", strings.Repeat("ab", 32))
	if id, _, err := g.AuthAgent(req); err == nil {
		t.Fatalf("spoofed fingerprint header accepted as %q", id)
	}
}

func TestGatewayAuthAgentUsesProxiedCert(t *testing.T) {
	der := testCertDER(t)
	want := sha256.Sum256(der)
	fp := hex.EncodeToString(want[:])
	g := NewGatewayAuthenticator("secret", "iss", "aud", false, loopbackOnly, staticPCs{fp: 42})
	id, pc, err := g.AuthAgent(proxiedRequest("127.0.0.1:1", "SUCCESS", nginxEscaped(der)))
	if err != nil || id != fp || pc != 42 {
		t.Fatalf("got %q, %d, %v", id, pc, err)
	}
}

func TestParseTrustedProxiesLoopbackOnly(t *testing.T) {
	got, err := ParseTrustedProxies(" 127.0.0.1, ::1 ,")
	if err != nil || len(got) != 2 {
		t.Fatalf("got %v, %v", got, err)
	}
	if got, err := ParseTrustedProxies(""); err != nil || len(got) != 0 {
		t.Fatalf("empty list: got %v, %v", got, err)
	}
	for _, bad := range []string{"192.168.10.2", "0.0.0.0", "127.0.0.1,10.0.0.1", "localhost", "127.0.0.0/8"} {
		if _, err := ParseTrustedProxies(bad); err == nil {
			t.Errorf("%q accepted", bad)
		}
	}
}

func TestGatewayAuthAgentRejectsSharedToken(t *testing.T) {
	g := NewGatewayAuthenticator("secret", "iss", "aud", false, loopbackOnly, staticPCs{})
	req := proxiedRequest("127.0.0.1:1", "NONE", "")
	req.Header.Set("X-Agent-Token", "sec-123")
	req.URL.RawQuery = "agent_token=sec-123"
	if id, _, err := g.AuthAgent(req); !errors.Is(err, ErrMissingCert) {
		t.Fatalf("shared token accepted as %q (%v)", id, err)
	}
}
