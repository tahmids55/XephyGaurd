package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"net/netip"
	"os"
	"strconv"
	"strings"
	"time"
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
	validator      *TokenValidator
	devInsecure    bool
	trustedProxies []netip.Addr
	pcs            PCResolver
}

func NewGatewayAuthenticator(secret, issuer, audience string, devInsecure bool, trustedProxies []netip.Addr, pcs PCResolver) *GatewayAuthenticator {
	var tv *TokenValidator
	if secret != "" {
		tv = NewTokenValidator(AuthConfig{
			JWTSecret: []byte(secret),
			Issuer:    issuer,
			Audience:  audience,
		})
	}
	return &GatewayAuthenticator{
		validator:      tv,
		devInsecure:    devInsecure,
		trustedProxies: trustedProxies,
		pcs:            pcs,
	}
}

func (g *GatewayAuthenticator) AuthViewer(r *http.Request) (ViewerGrant, error) {
	if g.validator != nil {
		if r.Header.Get("Authorization") != "" {
			return g.validator.AuthViewer(r)
		}
	}
	if !g.devInsecure {
		return ViewerGrant{}, ErrMissingToken
	}
	if sid := devStreamID(r); sid != 0 {
		mode := r.URL.Query().Get("mode")
		if mode == "" {
			mode = "live"
		}
		if !validModes[mode] {
			return ViewerGrant{}, ErrInvalidMode
		}
		log.Printf("[Gateway Auth] DEV-INSECURE viewer stream_id=%d mode=%s", sid, mode)
		return ViewerGrant{StreamID: sid, PCID: sid, Mode: mode, Exp: time.Now().Add(time.Hour)}, nil
	}
	return ViewerGrant{}, ErrMissingToken
}

func (g *GatewayAuthenticator) AuthViewerToken(token string) (ViewerGrant, error) {
	if g.validator == nil {
		return ViewerGrant{}, ErrMissingToken
	}
	return g.validator.ValidateToken(token)
}

func (g *GatewayAuthenticator) RenewViewer(token string) (ViewerGrant, error) {
	if g.validator == nil {
		return ViewerGrant{}, ErrMissingToken
	}
	return g.validator.ValidateToken(token)
}

func (g *GatewayAuthenticator) AuthAgent(r *http.Request) (string, uint32, error) {
	if id, err := AgentCertFingerprint(r, g.trustedProxies); err == nil {
		if g.pcs == nil {
			if g.devInsecure {
				if pc := devStreamID(r); pc != 0 {
					return id, pc, nil
				}
			}
			return "", 0, ErrUnknownAgent
		}
		pc, err := g.pcs.ResolvePC(r.Context(), id)
		if err != nil {
			return "", 0, err
		}
		return id, pc, nil
	}
	if g.devInsecure {
		if pc := devStreamID(r); pc != 0 {
			log.Printf("[Gateway Auth] DEV-INSECURE allowing unauthenticated agent from %s as stream %d", r.RemoteAddr, pc)
			return "dev-agent", pc, nil
		}
	}
	return "", 0, ErrMissingCert
}

// devStreamID reads ?stream_id for -dev-insecure only (no server lookup in dev).
func devStreamID(r *http.Request) uint32 {
	if parsed, err := strconv.ParseUint(r.URL.Query().Get("stream_id"), 10, 32); err == nil {
		return uint32(parsed)
	}
	return 0
}

func main() {
	addr := flag.String("addr", envOrDefault("SCREEN_GATEWAY_ADDR", "127.0.0.1:8082"), "Gateway HTTP/WebSocket address")
	secret := flag.String("secret", envOrDefault("SCREEN_GATEWAY_SECRET", ""), "JWT Secret")
	issuer := flag.String("issuer", envOrDefault("SCREEN_GATEWAY_ISSUER", "xephyguard-ccc"), "JWT Issuer")
	audience := flag.String("audience", envOrDefault("SCREEN_GATEWAY_AUDIENCE", "xephyguard-gateway"), "JWT Audience")
	trustedProxy := flag.String("trusted-proxy", envOrDefault("SCREEN_GATEWAY_TRUSTED_PROXY", ""), "Comma-separated loopback IPs of the TLS proxy whose X-SSL-Client-* headers are trusted (empty: none)")
	serverURL := flag.String("server-url", envOrDefault("SCREEN_GATEWAY_SERVER_URL", "http://127.0.0.1:8000"), "CCC server base URL for the internal agent-to-PC lookup")
	allowedOrigins := flag.String("allowed-origins", envOrDefault("SCREEN_GATEWAY_ALLOWED_ORIGINS", ""), "Comma-separated extra browser Origin hosts (host[:port], * wildcards) allowed besides the request host")
	devInsecure := flag.Bool("dev-insecure", envBool("SCREEN_GATEWAY_DEV_INSECURE", false), "Allow unauthenticated dev connections (never use in production)")
	flag.Parse()

	if *devInsecure && !isLoopbackAddr(*addr) {
		log.Fatalf("-dev-insecure only with a loopback -addr (got %q)", *addr)
	}
	if !*devInsecure && *secret == "" {
		log.Fatal("SCREEN_GATEWAY_SECRET (-secret) is required unless -dev-insecure is explicitly set")
	}

	// Env only (not a flag) so the token does not show up in the process list.
	internalToken := strings.TrimSpace(os.Getenv("SCREEN_GATEWAY_INTERNAL_TOKEN"))
	var pcs PCResolver
	if internalToken != "" {
		pcs = NewServerPCResolver(*serverURL, internalToken)
	} else if !*devInsecure {
		log.Fatal("SCREEN_GATEWAY_INTERNAL_TOKEN is required unless -dev-insecure is explicitly set")
	}

	trusted, err := ParseTrustedProxies(*trustedProxy)
	if err != nil {
		log.Fatalf("-trusted-proxy: %v", err)
	}

	auth := NewGatewayAuthenticator(*secret, *issuer, *audience, *devInsecure, trusted, pcs)
	relay := NewRelayServer(auth)
	relay.internalToken = internalToken // enables POST /internal/revoke
	relay.originPatterns = splitList(*allowedOrigins)

	server := &http.Server{
		Addr:              *addr,
		Handler:           relay.Routes(),
		ReadHeaderTimeout: 10 * time.Second,
		IdleTimeout:       120 * time.Second,
	}
	go relay.RunJanitor(context.Background())

	fmt.Printf("XephyGuard Screen Gateway listening on %s\n", *addr)
	if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		log.Fatalf("Gateway server failed: %v", err)
	}
}

// splitList splits a comma-separated flag value, dropping empty items.
func splitList(v string) []string {
	var out []string
	for _, p := range strings.Split(v, ",") {
		if p = strings.TrimSpace(p); p != "" {
			out = append(out, p)
		}
	}
	return out
}

// isLoopbackAddr reports whether a listen address binds loopback only (127.0.0.0/8, ::1 or localhost).
// An empty host (":8082") binds every interface and is not loopback.
func isLoopbackAddr(addr string) bool {
	host, _, err := net.SplitHostPort(addr)
	if err != nil || host == "" {
		return false
	}
	if host == "localhost" {
		return true
	}
	ip, err := netip.ParseAddr(host)
	return err == nil && ip.IsLoopback()
}
