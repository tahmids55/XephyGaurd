package main

import (
	"crypto/subtle"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"strconv"
	"strings"
)

func envOrDefault(key, def string) string {
	if v := strings.TrimSpace(os.Getenv(key)); v != "" {
		return v
	}
	return def
}

func envBool(key string, def bool) bool {
	v := strings.TrimSpace(strings.ToLower(os.Getenv(key)))
	switch v {
	case "1", "true", "yes", "on":
		return true
	case "0", "false", "no", "off":
		return false
	default:
		return def
	}
}

type GatewayAuthenticator struct {
	validator   *TokenValidator
	agentSecret string
	devInsecure bool
}

func NewGatewayAuthenticator(secret, agentSecret, issuer, audience string, devInsecure bool) *GatewayAuthenticator {
	var tv *TokenValidator
	if secret != "" {
		tv = NewTokenValidator(AuthConfig{
			JWTSecret: []byte(secret),
			Issuer:    issuer,
			Audience:  audience,
		})
	}
	return &GatewayAuthenticator{
		validator:   tv,
		agentSecret: agentSecret,
		devInsecure: devInsecure,
	}
}

func (g *GatewayAuthenticator) AuthViewer(r *http.Request) (uint32, error) {
	if g.validator != nil {
		if r.URL.Query().Get("token") != "" || r.Header.Get("Authorization") != "" {
			return g.validator.AuthViewer(r)
		}
	}
	if !g.devInsecure {
		return 0, ErrMissingToken
	}
	if sidStr := r.URL.Query().Get("stream_id"); sidStr != "" {
		if parsed, err := strconv.ParseUint(sidStr, 10, 32); err == nil && parsed > 0 {
			log.Printf("[Gateway Auth] DEV-INSECURE viewer stream_id=%d", parsed)
			return uint32(parsed), nil
		}
	}
	return 0, ErrMissingToken
}

func (g *GatewayAuthenticator) AuthAgent(r *http.Request) (string, error) {
	if g.validator != nil {
		if id, err := g.validator.AuthAgent(r); err == nil {
			return id, nil
		}
	}
	if g.agentSecret != "" {
		tok := r.Header.Get("X-Agent-Token")
		if tok == "" {
			tok = r.URL.Query().Get("agent_token")
		}
		if len(tok) == len(g.agentSecret) &&
			subtle.ConstantTimeCompare([]byte(tok), []byte(g.agentSecret)) == 1 {
			return "agent-token", nil
		}
	}
	if g.devInsecure {
		log.Printf("[Gateway Auth] DEV-INSECURE allowing unauthenticated agent from %s", r.RemoteAddr)
		return "dev-agent", nil
	}
	return "", ErrMissingCert
}

func main() {
	addr := flag.String("addr", envOrDefault("SCREEN_GATEWAY_ADDR", ":8082"), "Gateway HTTP/WebSocket address")
	secret := flag.String("secret", envOrDefault("SCREEN_GATEWAY_SECRET", ""), "JWT Secret")
	agentSecret := flag.String("agent-secret", envOrDefault("SCREEN_GATEWAY_AGENT_SECRET", ""), "Shared token for screen agents")
	issuer := flag.String("issuer", envOrDefault("SCREEN_GATEWAY_ISSUER", "xephyguard-ccc"), "JWT Issuer")
	audience := flag.String("audience", envOrDefault("SCREEN_GATEWAY_AUDIENCE", "xephyguard-gateway"), "JWT Audience")
	devInsecure := flag.Bool("dev-insecure", envBool("SCREEN_GATEWAY_DEV_INSECURE", false), "Allow unauthenticated dev connections (never use in production)")
	flag.Parse()

	if !*devInsecure && *secret == "" {
		log.Fatal("SCREEN_GATEWAY_SECRET (-secret) is required unless -dev-insecure is explicitly set")
	}

	auth := NewGatewayAuthenticator(*secret, *agentSecret, *issuer, *audience, *devInsecure)
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
