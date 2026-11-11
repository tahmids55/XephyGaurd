package main

import (
	"flag"
	"fmt"
	"log"
	"net/http"
)

func main() {
	addr := flag.String("addr", ":8082", "Gateway HTTP/WebSocket address")
	flag.Parse()

	// Default authenticator for standalone mode
	auth := &StaticAuthenticator{}
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

type StaticAuthenticator struct{}

func (s *StaticAuthenticator) AuthViewer(r *http.Request) (uint32, error) {
	// Standalone stub for dev
	return 1, nil
}

func (s *StaticAuthenticator) AuthAgent(r *http.Request) (string, error) {
	// Standalone stub for dev
	return "dev-agent", nil
}
