package main

import (
	"context"
	"net/http"
	"net/http/httptest"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"
)

func revokeReq(remote, auth, method, body string) *http.Request {
	r := httptest.NewRequest(method, "/internal/revoke", strings.NewReader(body))
	r.RemoteAddr = remote
	if auth != "" {
		r.Header.Set("Authorization", auth)
	}
	return r
}

func TestRevokeEndpointGuards(t *testing.T) {
	relay := NewRelayServer(&FakeAuth{})
	h := relay.Routes()
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, revokeReq("127.0.0.1:5", "Bearer tok", "POST", `{"user_id":1}`))
	if rec.Code != http.StatusNotFound {
		t.Fatalf("no internal token configured: got %d, want 404", rec.Code)
	}

	relay.internalToken = "tok"
	cases := []struct {
		name string
		req  *http.Request
		want int
	}{
		{"LAN address", revokeReq("192.168.10.5:5", "Bearer tok", "POST", `{"user_id":1}`), http.StatusForbidden},
		{"no token", revokeReq("127.0.0.1:5", "", "POST", `{"user_id":1}`), http.StatusUnauthorized},
		{"wrong token", revokeReq("127.0.0.1:5", "Bearer nope", "POST", `{"user_id":1}`), http.StatusUnauthorized},
		{"GET", revokeReq("127.0.0.1:5", "Bearer tok", "GET", ``), http.StatusMethodNotAllowed},
		{"empty selector", revokeReq("127.0.0.1:5", "Bearer tok", "POST", `{}`), http.StatusBadRequest},
		{"not json", revokeReq("127.0.0.1:5", "Bearer tok", "POST", `user=1`), http.StatusBadRequest},
		{"ok", revokeReq("[::1]:5", "Bearer tok", "POST", `{"lab_session_id":3}`), http.StatusOK},
	}
	for _, c := range cases {
		rec := httptest.NewRecorder()
		h.ServeHTTP(rec, c.req)
		if rec.Code != c.want {
			t.Errorf("%s: got %d, want %d", c.name, rec.Code, c.want)
		}
	}
}

func TestRevocationsBlockOnlyEarlierTokens(t *testing.T) {
	r := newRevocations()
	t0 := time.Now()
	r.add(revokeRequest{UserID: 7}, t0)
	if !r.blocks(ViewerGrant{UserID: 7, IssuedAt: t0.Add(-time.Second)}) {
		t.Fatal("token issued before the revoke must be blocked")
	}
	if r.blocks(ViewerGrant{UserID: 7, IssuedAt: t0.Add(time.Second)}) {
		t.Fatal("token issued after the revoke (new login) must pass")
	}
	if r.blocks(ViewerGrant{UserID: 8, IssuedAt: t0.Add(-time.Second)}) {
		t.Fatal("other user blocked")
	}
	r.add(revokeRequest{AuditID: 5}, t0)
	if !r.blocks(ViewerGrant{UserID: 9, AuditID: 5, IssuedAt: t0}) {
		t.Fatal("revoked audit not blocked")
	}
}

func TestRevokeClosesMatchingViewersOnly(t *testing.T) {
	// ?user=N selects the lease's user id; audit = 100 + user.
	auth := &FakeAuth{userFn: func(r *http.Request) (uint64, uint64) {
		u, _ := strconv.ParseUint(r.URL.Query().Get("user"), 10, 64)
		return u, 100 + u
	}}
	relay := NewRelayServer(auth)
	relay.internalToken = "tok"
	srv := httptest.NewServer(relay.Routes())
	defer srv.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	victim, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/view")+"?stream_id=70&user=1", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer victim.Close(websocket.StatusNormalClosure, "")
	other, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/view")+"?stream_id=70&user=2", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer other.Close(websocket.StatusNormalClosure, "")
	time.Sleep(100 * time.Millisecond) // both registered

	req, _ := http.NewRequest("POST", srv.URL+"/internal/revoke", strings.NewReader(`{"audit_id":101}`))
	req.Header.Set("Authorization", "Bearer tok")
	resp, err := http.DefaultClient.Do(req)
	if err != nil || resp.StatusCode != http.StatusOK {
		t.Fatalf("revoke: %v %v", resp, err)
	}
	resp.Body.Close()

	if code, _ := readUntilClose(ctx, t, victim); code != kCloseRevoked {
		t.Fatalf("victim close code %d, want %d", code, kCloseRevoked)
	}
	rctx, rcancel := context.WithTimeout(ctx, 300*time.Millisecond)
	defer rcancel()
	if _, _, err := other.Read(rctx); websocket.CloseStatus(err) != -1 {
		t.Fatalf("other viewer was closed: %v", err)
	}
}
