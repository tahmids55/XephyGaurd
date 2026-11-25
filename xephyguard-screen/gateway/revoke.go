package main

import (
	"crypto/subtle"
	"encoding/json"
	"io"
	"net/http"
	"net/netip"
	"strconv"
	"sync"
	"time"

	"github.com/coder/websocket"
)

// kCloseRevoked is the WebSocket close code sent when the server revokes a viewer lease.
const kCloseRevoked websocket.StatusCode = 4003

// revocationTTL is how long a revocation is remembered; longer than any lease the server issues.
const revocationTTL = time.Hour

// revokeRequest selects leases to end (screen Step 5). Any non-zero field matches.
type revokeRequest struct {
	AuditID      uint64 `json:"audit_id"`       // one view stopped
	UserID       uint64 `json:"user_id"`        // the user logged out or was deactivated
	LabSessionID uint64 `json:"lab_session_id"` // the session ended
}

func (q revokeRequest) matches(g ViewerGrant) bool {
	return (q.AuditID != 0 && q.AuditID == g.AuditID) ||
		(q.UserID != 0 && q.UserID == g.UserID) ||
		(q.LabSessionID != 0 && q.LabSessionID == g.LabSessionID)
}

// revocations remembers when each audit, user or session was revoked, so tokens issued before
// that moment are refused on connect and on renewal (a token is valid for its whole TTL otherwise).
type revocations struct {
	mu sync.Mutex
	at map[string]time.Time
}

func newRevocations() *revocations { return &revocations{at: make(map[string]time.Time)} }

func revocationKeys(audit, user, session uint64) []string {
	var keys []string
	if audit != 0 {
		keys = append(keys, "a:"+strconv.FormatUint(audit, 10))
	}
	if user != 0 {
		keys = append(keys, "u:"+strconv.FormatUint(user, 10))
	}
	if session != 0 {
		keys = append(keys, "s:"+strconv.FormatUint(session, 10))
	}
	return keys
}

func (r *revocations) add(q revokeRequest, now time.Time) {
	r.mu.Lock()
	defer r.mu.Unlock()
	for k, t := range r.at {
		if now.Sub(t) > revocationTTL {
			delete(r.at, k)
		}
	}
	for _, k := range revocationKeys(q.AuditID, q.UserID, q.LabSessionID) {
		r.at[k] = now
	}
}

// blocks reports whether g was issued at or before a revocation that matches it.
func (r *revocations) blocks(g ViewerGrant) bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	for _, k := range revocationKeys(g.AuditID, g.UserID, g.LabSessionID) {
		if t, ok := r.at[k]; ok && !g.IssuedAt.After(t) {
			return true
		}
	}
	return false
}

// HandleRevoke ends matching viewer leases: POST /internal/revoke from loopback with
// "Authorization: Bearer <internal token>" and a JSON revokeRequest. Answers {"closed": N}.
func (s *RelayServer) HandleRevoke(w http.ResponseWriter, r *http.Request) {
	if s.internalToken == "" {
		http.NotFound(w, r)
		return
	}
	ap, err := netip.ParseAddrPort(r.RemoteAddr)
	if err != nil || !ap.Addr().Unmap().IsLoopback() {
		http.Error(w, "forbidden", http.StatusForbidden)
		return
	}
	want := []byte("Bearer " + s.internalToken)
	if subtle.ConstantTimeCompare([]byte(r.Header.Get("Authorization")), want) != 1 {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var q revokeRequest
	if err := json.NewDecoder(io.LimitReader(r.Body, 4096)).Decode(&q); err != nil ||
		(q.AuditID == 0 && q.UserID == 0 && q.LabSessionID == 0) {
		http.Error(w, "bad request", http.StatusBadRequest)
		return
	}

	s.revoked.add(q, time.Now())
	closed := s.closeViewers(q)

	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]int{"closed": closed})
}

// closeViewers closes every open viewer whose lease matches q.
func (s *RelayServer) closeViewers(q revokeRequest) int {
	var conns []*websocket.Conn
	s.mu.RLock()
	for _, st := range s.streams {
		st.mu.Lock()
		for _, v := range st.viewers {
			if q.matches(v.grant) && v.conn != nil {
				conns = append(conns, v.conn)
			}
		}
		st.mu.Unlock()
	}
	s.mu.RUnlock()
	for _, c := range conns {
		go c.Close(kCloseRevoked, "lease revoked")
	}
	return len(conns)
}
