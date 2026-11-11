package main

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"math/big"
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
		StreamID:  42,
	}

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	tokenStr, err := token.SignedString(secret)
	if err != nil {
		t.Fatalf("token sign failed: %v", err)
	}

	req := httptest.NewRequest("GET", "/view?token="+tokenStr, nil)
	streamID, err := tv.AuthViewer(req)
	if err != nil {
		t.Fatalf("expected valid token to pass, got: %v", err)
	}
	if streamID != 42 {
		t.Fatalf("expected streamID 42, got %d", streamID)
	}

	// 2. Replay of same token -> MUST BE REJECTED
	reqReplay := httptest.NewRequest("GET", "/view?token="+tokenStr, nil)
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
	reqExpired := httptest.NewRequest("GET", "/view?token="+expiredStr, nil)
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
	reqWrongAud := httptest.NewRequest("GET", "/view?token="+wrongAudStr, nil)
	_, errWrongAud := tv.AuthViewer(reqWrongAud)
	if errWrongAud == nil {
		t.Fatalf("expected wrong audience token to fail")
	}

	// 5. Wrong algorithm (e.g. none) -> MUST BE REJECTED
	noneToken := jwt.NewWithClaims(jwt.SigningMethodNone, claims)
	noneStr, _ := noneToken.SignedString(jwt.UnsafeAllowNoneSignatureType)
	reqNone := httptest.NewRequest("GET", "/view?token="+noneStr, nil)
	_, errNone := tv.AuthViewer(reqNone)
	if errNone == nil {
		t.Fatalf("expected none algorithm token to fail")
	}
}

func TestAuthAgentMTLSFingerprint(t *testing.T) {
	tv := NewTokenValidator(AuthConfig{})

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

	fp, err := tv.AuthAgent(req)
	if err != nil {
		t.Fatalf("expected mTLS auth to succeed, got: %v", err)
	}
	if fp != expectedFP {
		t.Fatalf("expected fingerprint %s, got %s", expectedFP, fp)
	}
}
