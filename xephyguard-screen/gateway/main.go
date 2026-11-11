package main

import (
	"flag"
	"fmt"
	"log"
	"net/http"
	"strconv"
)

type FlexibleAuthenticator struct {
	validator *TokenValidator
}

func NewFlexibleAuthenticator(secret, issuer, audience string) *FlexibleAuthenticator {
	return &FlexibleAuthenticator{
		validator: NewTokenValidator(AuthConfig{
			JWTSecret: []byte(secret),
			Issuer:    issuer,
			Audience:  audience,
		}),
	}
}

func (f *FlexibleAuthenticator) AuthViewer(r *http.Request) (uint32, error) {
	// 1. Try JWT Token authentication
	if r.URL.Query().Get("token") != "" || r.Header.Get("Authorization") != "" {
		sid, err := f.validator.AuthViewer(r)
		if err == nil {
			log.Printf("[Gateway Auth] Verified viewer JWT token: stream_id=%d", sid)
			return sid, nil
		}
		log.Printf("[Gateway Auth] JWT token check failed (%v), checking query params", err)
	}

	// 2. Fallback to ?stream_id= param
	if sidStr := r.URL.Query().Get("stream_id"); sidStr != "" {
		if parsed, err := strconv.ParseUint(sidStr, 10, 32); err == nil {
			log.Printf("[Gateway Auth] Viewer authenticated via stream_id param=%d", parsed)
			return uint32(parsed), nil
		}
	}

	log.Printf("[Gateway Auth] Viewer default stream_id=1")
	return 1, nil
}

func (f *FlexibleAuthenticator) AuthAgent(r *http.Request) (string, error) {
	return "agent", nil
}

func main() {
	addr := flag.String("addr", ":8082", "Gateway HTTP/WebSocket address")
	secret := flag.String("secret", "dev-secret-change-in-production-12345", "JWT Secret")
	issuer := flag.String("issuer", "xephyguard-ccc", "JWT Issuer")
	audience := flag.String("audience", "xephyguard-gateway", "JWT Audience")
	flag.Parse()

	auth := NewFlexibleAuthenticator(*secret, *issuer, *audience)
	relay := NewRelayServer(auth)

	server := &http.Server{
		Addr:    *addr,
		Handler: relay.Routes(),
	}

	fmt.Printf("XephyGuard Screen Gateway listening on %s\n", *addr)
	if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		log.Fatalf("Gateway server failed: %v", err)
	}
}
