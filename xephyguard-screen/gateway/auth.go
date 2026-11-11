package main

import (
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/golang-jwt/jwt/v5"
)

var (
	ErrMissingToken       = errors.New("missing authorization token")
	ErrReplayedToken      = errors.New("replayed JWT token rejected (jti reused)")
	ErrInvalidAlgorithm   = errors.New("invalid signing algorithm (only HS256 allowed)")
	ErrMissingCert        = errors.New("missing mTLS client certificate")
	ErrInvalidClaims      = errors.New("invalid token claims")
	ErrTokenExpiredOrEarly = errors.New("token expired or not yet valid")
)

type ViewerClaims struct {
	jwt.RegisteredClaims
	StudentID    uint32 `json:"student_id"`
	LabSessionID uint32 `json:"lab_session_id"`
	StreamID     uint32 `json:"stream_id"`
	Mode         string `json:"mode"`
}

type AuthConfig struct {
	JWTSecret []byte
	Issuer    string
	Audience  string
}

type TokenValidator struct {
	config AuthConfig
	mu     sync.Mutex
	used   map[string]time.Time // jti -> expiresAt
}

func NewTokenValidator(cfg AuthConfig) *TokenValidator {
	return &TokenValidator{
		config: cfg,
		used:   make(map[string]time.Time),
	}
}

func (tv *TokenValidator) cleanupExpiredLocked(now time.Time) {
	for jti, exp := range tv.used {
		if now.After(exp) {
			delete(tv.used, jti)
		}
	}
}

func (tv *TokenValidator) AuthViewer(r *http.Request) (uint32, error) {
	tokenStr := ""

	// Check Authorization header: Bearer <token>
	authHeader := r.Header.Get("Authorization")
	if strings.HasPrefix(authHeader, "Bearer ") {
		tokenStr = strings.TrimPrefix(authHeader, "Bearer ")
	}

	// Check query string ?token=...
	if tokenStr == "" {
		tokenStr = r.URL.Query().Get("token")
	}

	if tokenStr == "" {
		return 0, ErrMissingToken
	}

	claims := &ViewerClaims{}
	token, err := jwt.ParseWithClaims(tokenStr, claims, func(t *jwt.Token) (interface{}, error) {
		// Pin signing algorithm strictly to HS256
		if t.Method == nil || t.Method.Alg() != jwt.SigningMethodHS256.Alg() {
			return nil, fmt.Errorf("%w: got %v", ErrInvalidAlgorithm, t.Header["alg"])
		}
		return tv.config.JWTSecret, nil
	}, jwt.WithIssuer(tv.config.Issuer), jwt.WithAudience(tv.config.Audience))

	if err != nil {
		return 0, err
	}

	if !token.Valid || claims.ID == "" {
		return 0, ErrInvalidClaims
	}

	now := time.Now()

	// Check and mark jti in TTL map
	tv.mu.Lock()
	defer tv.mu.Unlock()

	tv.cleanupExpiredLocked(now)

	if exp, exists := tv.used[claims.ID]; exists && now.Before(exp) {
		return 0, ErrReplayedToken
	}

	expiresAt := now.Add(5 * time.Minute)
	if claims.ExpiresAt != nil {
		expiresAt = claims.ExpiresAt.Time
	}
	tv.used[claims.ID] = expiresAt

	streamID := claims.StreamID
	if streamID == 0 {
		streamID = claims.StudentID
	}

	return streamID, nil
}

func (tv *TokenValidator) AuthAgent(r *http.Request) (string, error) {
	// If mTLS client certificate is present
	if r.TLS != nil && len(r.TLS.PeerCertificates) > 0 {
		cert := r.TLS.PeerCertificates[0]
		h := sha256.Sum256(cert.Raw)
		return hex.EncodeToString(h[:]), nil
	}

	// Fallback for dev / proxy header if explicitly forwarded
	if clientFP := r.Header.Get("X-Agent-Cert-Fingerprint"); clientFP != "" {
		return clientFP, nil
	}

	return "", ErrMissingCert
}
