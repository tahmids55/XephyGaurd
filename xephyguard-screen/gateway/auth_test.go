package main

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"errors"
	"math/big"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/golang-jwt/jwt/v5"
)

func TestAuthViewerValidationAndReplayPrevention(t *testing.T) {
	secret := []byte("top-secret-signing-key-32-bytes!")
	cfg := AuthConfig{
		JWTSecret: secret,
		Issuer:    "xephyguard-ccc",
		Audience:  "xephyguard-gateway",
	}
	tv := NewTokenValidator(cfg)

	now := time.Now()

	// 1. Valid token
	claims := ViewerClaims{
		RegisteredClaims: jwt.RegisteredClaims{
			Issuer:    "xephyguard-ccc",
			Audience:  jwt.ClaimStrings{"xephyguard-gateway"},
			ID:        "unique-jti-1",
			IssuedAt:  jwt.NewNumericDate(now),
			NotBefore: jwt.NewNumericDate(now.Add(-1 * time.Second)),
			ExpiresAt: jwt.NewNumericDate(now.Add(30 * time.Second)),
		},
		StudentID: 42,
		PCID:      42,
		StreamID:  42,
		Mode:      "live",
	}

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	tokenStr, err := token.SignedString(secret)
	if err != nil {
		t.Fatalf("token sign failed: %v", err)
	}

	req := bearerReq(tokenStr)
	grant, err := tv.AuthViewer(req)
	if err != nil {
		t.Fatalf("expected valid token to pass, got: %v", err)
	}
	if grant.StreamID != 42 || grant.PCID != 42 || grant.Mode != "live" || !grant.Exp.Equal(claims.ExpiresAt.Time) {
		t.Fatalf("unexpected grant %+v", grant)
	}

	// 2. Replay of same token -> MUST BE REJECTED
	reqReplay := bearerReq(tokenStr)
	_, errReplay := tv.AuthViewer(reqReplay)
	if errReplay == nil {
		t.Fatalf("expected replayed token to be rejected, but it succeeded")
	}
	if errReplay != ErrReplayedToken {
		t.Fatalf("expected ErrReplayedToken, got: %v", errReplay)
	}

	// 3. Expired token -> MUST BE REJECTED
	expiredClaims := ViewerClaims{
		RegisteredClaims: jwt.RegisteredClaims{
			Issuer:    "xephyguard-ccc",
			Audience:  jwt.ClaimStrings{"xephyguard-gateway"},
			ID:        "unique-jti-expired",
			IssuedAt:  jwt.NewNumericDate(now.Add(-60 * time.Second)),
			ExpiresAt: jwt.NewNumericDate(now.Add(-10 * time.Second)),
		},
		StudentID: 55,
	}
	expiredToken := jwt.NewWithClaims(jwt.SigningMethodHS256, expiredClaims)
	expiredStr, _ := expiredToken.SignedString(secret)
	reqExpired := bearerReq(expiredStr)
	_, errExpired := tv.AuthViewer(reqExpired)
	if errExpired == nil {
		t.Fatalf("expected expired token to fail")
	}

	// 4. Wrong audience token -> MUST BE REJECTED
	wrongAudClaims := ViewerClaims{
		RegisteredClaims: jwt.RegisteredClaims{
			Issuer:    "xephyguard-ccc",
			Audience:  jwt.ClaimStrings{"wrong-aud"},
			ID:        "unique-jti-wrong-aud",
			ExpiresAt: jwt.NewNumericDate(now.Add(30 * time.Second)),
		},
		StudentID: 66,
	}
	wrongAudToken := jwt.NewWithClaims(jwt.SigningMethodHS256, wrongAudClaims)
	wrongAudStr, _ := wrongAudToken.SignedString(secret)
	reqWrongAud := bearerReq(wrongAudStr)
	_, errWrongAud := tv.AuthViewer(reqWrongAud)
	if errWrongAud == nil {
		t.Fatalf("expected wrong audience token to fail")
	}

	// 5. Wrong algorithm (e.g. none) -> MUST BE REJECTED
	noneToken := jwt.NewWithClaims(jwt.SigningMethodNone, claims)
	noneStr, _ := noneToken.SignedString(jwt.UnsafeAllowNoneSignatureType)
	reqNone := bearerReq(noneStr)
	_, errNone := tv.AuthViewer(reqNone)
	if errNone == nil {
		t.Fatalf("expected none algorithm token to fail")
	}
}

func TestAuthAgentMTLSFingerprint(t *testing.T) {
	// Generate a dummy self-signed cert in memory
	priv, _ := rsa.GenerateKey(rand.Reader, 2048)
	template := x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject:      pkix.Name{CommonName: "agent-test"},
		NotBefore:    time.Now().Add(-1 * time.Hour),
		NotAfter:     time.Now().Add(1 * time.Hour),
	}
	certBytes, err := x509.CreateCertificate(rand.Reader, &template, &template, &priv.PublicKey, priv)
	if err != nil {
		t.Fatalf("failed to create dummy cert: %v", err)
	}
	cert, _ := x509.ParseCertificate(certBytes)

	h := sha256.Sum256(certBytes)
	expectedFP := hex.EncodeToString(h[:])

	req := httptest.NewRequest("GET", "/agent", nil)
	req.TLS = &tls.ConnectionState{PeerCertificates: []*x509.Certificate{cert}}

	fp, err := AgentCertFingerprint(req, nil)
	if err != nil {
		t.Fatalf("expected mTLS auth to succeed, got: %v", err)
	}
	if fp != expectedFP {
		t.Fatalf("expected fingerprint %s, got %s", expectedFP, fp)
	}
}

func TestAuthViewerRejectsBadStreamPCAndMode(t *testing.T) {
	secret := []byte("top-secret-signing-key-32-bytes!")
	tv := NewTokenValidator(AuthConfig{JWTSecret: secret, Issuer: "iss", Audience: "aud"})
	now := time.Now()
	mk := func(jti string, pc, stream, student uint32, mode string, exp bool) *http.Request {
		rc := jwt.RegisteredClaims{Issuer: "iss", Audience: jwt.ClaimStrings{"aud"}, ID: jti, IssuedAt: jwt.NewNumericDate(now)}
		if exp {
			rc.ExpiresAt = jwt.NewNumericDate(now.Add(30 * time.Second))
		}
		tok, err := jwt.NewWithClaims(jwt.SigningMethodHS256, ViewerClaims{
			RegisteredClaims: rc, PCID: pc, StreamID: stream, StudentID: student, Mode: mode,
		}).SignedString(secret)
		if err != nil {
			t.Fatal(err)
		}
		return bearerReq(tok)
	}
	cases := []struct {
		name string
		req  *http.Request
	}{
		{"zero stream, student fallback gone", mk("a", 0, 0, 9, "live", true)},
		{"missing pc_id", mk("b", 0, 9, 0, "live", true)},
		{"pc_id differs from stream_id", mk("c", 8, 9, 0, "live", true)},
		{"unknown mode", mk("d", 9, 9, 0, "control", true)},
		{"empty mode", mk("e", 9, 9, 0, "", true)},
		{"no exp", mk("f", 9, 9, 0, "live", false)},
	}
	for _, c := range cases {
		if g, err := tv.AuthViewer(c.req); err == nil {
			t.Errorf("%s: accepted as %+v", c.name, g)
		}
	}
	for _, mode := range []string{"live", "snapshot", "wall"} {
		if g, err := tv.AuthViewer(mk("ok-"+mode, 9, 9, 0, mode, true)); err != nil || g.Mode != mode {
			t.Errorf("mode %s: got %+v, %v", mode, g, err)
		}
	}
}

// bearerReq is a viewer upgrade request carrying the token in the Authorization header.
func bearerReq(tok string) *http.Request {
	r := httptest.NewRequest("GET", "/view", nil)
	r.Header.Set("Authorization", "Bearer "+tok)
	return r
}

// A token in the URL query is not accepted any more (screen Step 40).
func TestAuthViewerIgnoresQueryToken(t *testing.T) {
	tv := NewTokenValidator(AuthConfig{JWTSecret: []byte("0123456789abcdef0123456789abcdef"), Issuer: "i", Audience: "a"})
	_, err := tv.AuthViewer(httptest.NewRequest("GET", "/view?token=anything", nil))
	if !errors.Is(err, ErrMissingToken) {
		t.Fatalf("query token must count as missing, got %v", err)
	}
}
