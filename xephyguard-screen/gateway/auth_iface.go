// gateway/auth_iface.go  (FIXED)
package main

import (
	"net/http"
	"time"
)

// ViewerGrant is what a valid viewer lease allows: one PC's stream, in one mode, until Exp.
type ViewerGrant struct {
	StreamID uint32
	PCID     uint32
	Mode     string // live, snapshot or wall
	Exp      time.Time
	// Who and what the lease is for; used by POST /internal/revoke (0 = unknown).
	UserID       uint64
	AuditID      uint64
	LabSessionID uint64
	IssuedAt     time.Time
}

type Authenticator interface {
	// Returns the grant for this viewer. Error => HTTP 401 and close.
	AuthViewer(r *http.Request) (ViewerGrant, error)
	// Validates the lease token a browser viewer sends as its first WebSocket message (screen Step 40).
	// Same checks as at upgrade (signature, claims, single use). Error => the socket is closed (1008).
	AuthViewerToken(token string) (ViewerGrant, error)
	// Validates a renewed lease token sent on an open viewer socket. Error => renewal refused.
	RenewViewer(token string) (ViewerGrant, error)
	// Returns a stable agent ID (client-cert fingerprint) and the PC the certificate belongs to.
	// The agent may only stream as that PC (stream_id == pcID). Error => reject.
	AuthAgent(r *http.Request) (agentID string, pcID uint32, err error)
}
