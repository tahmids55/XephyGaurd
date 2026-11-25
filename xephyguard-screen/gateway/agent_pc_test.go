package main

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"
)

// staticPCs is a PCResolver backed by a map (tests only).
type staticPCs map[string]uint32

func (m staticPCs) ResolvePC(_ context.Context, fp string) (uint32, error) {
	if pc, ok := m[fp]; ok {
		return pc, nil
	}
	return 0, ErrUnknownAgent
}

func TestServerPCResolver(t *testing.T) {
	fp := strings.Repeat("ab", 32)
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("Authorization") != "Bearer tok" {
			w.WriteHeader(http.StatusUnauthorized)
			return
		}
		switch r.URL.Path {
		case "/api/internal/screen/agent/" + fp:
			fmt.Fprint(w, `{"pc_id":7}`)
		case "/api/internal/screen/agent/" + strings.Repeat("cd", 32):
			fmt.Fprint(w, `{"pc_id":0}`)
		default:
			w.WriteHeader(http.StatusNotFound)
		}
	}))
	defer srv.Close()
	ctx := context.Background()

	if pc, err := NewServerPCResolver(srv.URL+"/", "tok").ResolvePC(ctx, fp); err != nil || pc != 7 {
		t.Fatalf("known agent: got %d, %v", pc, err)
	}
	if _, err := NewServerPCResolver(srv.URL, "tok").ResolvePC(ctx, strings.Repeat("ef", 32)); !errors.Is(err, ErrUnknownAgent) {
		t.Fatalf("unknown agent: got %v", err)
	}
	if _, err := NewServerPCResolver(srv.URL, "tok").ResolvePC(ctx, strings.Repeat("cd", 32)); !errors.Is(err, ErrUnknownAgent) {
		t.Fatalf("pc_id 0: got %v", err)
	}
	if _, err := NewServerPCResolver(srv.URL, "bad").ResolvePC(ctx, fp); err == nil || errors.Is(err, ErrUnknownAgent) {
		t.Fatalf("bad token must be a lookup error, got %v", err)
	}
	for _, bad := range []string{"", "ab", strings.Repeat("AB", 32), strings.Repeat("ab", 31) + "/x", "../" + fp} {
		if _, err := NewServerPCResolver(srv.URL, "tok").ResolvePC(ctx, bad); !errors.Is(err, ErrUnknownAgent) {
			t.Errorf("malformed fingerprint %q: got %v", bad, err)
		}
	}
}

func TestAgentRefusedForAnotherPCsStream(t *testing.T) {
	relay := NewRelayServer(&FakeAuth{agentPC: 5})
	srv := httptest.NewServer(relay.Routes())
	defer srv.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	_, resp, err := websocket.Dial(ctx, toWSURL(srv.URL, "/agent")+"?stream_id=6", nil)
	if err == nil || resp == nil || resp.StatusCode != http.StatusForbidden {
		t.Fatalf("expected 403 for another PC's stream, got %v", resp)
	}
	_, resp, err = websocket.Dial(ctx, toWSURL(srv.URL, "/agent")+"?stream_id=abc", nil)
	if err == nil || resp == nil || resp.StatusCode != http.StatusForbidden {
		t.Fatalf("expected 403 for a malformed stream_id, got %v", resp)
	}
}

func TestAgentFramesForAnotherStreamAreDropped(t *testing.T) {
	relay := NewRelayServer(&FakeAuth{agentPC: 5})
	srv := httptest.NewServer(relay.Routes())
	defer srv.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	// Agent connects without ?stream_id: it is bound to its PC (5) anyway.
	agentConn, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/agent"), nil)
	if err != nil {
		t.Fatalf("agent dial: %v", err)
	}
	defer agentConn.Close(websocket.StatusNormalClosure, "")

	victim, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/view")+"?stream_id=6", nil)
	if err != nil {
		t.Fatalf("victim viewer dial: %v", err)
	}
	defer victim.Close(websocket.StatusNormalClosure, "")
	own, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/view")+"?stream_id=5", nil)
	if err != nil {
		t.Fatalf("own viewer dial: %v", err)
	}
	defer own.Close(websocket.StatusNormalClosure, "")

	if err := agentConn.Write(ctx, websocket.MessageBinary, makeTestFrame(6, 0, 1)); err != nil {
		t.Fatal(err)
	}
	if err := agentConn.Write(ctx, websocket.MessageBinary, makeTestFrame(5, 0, 2)); err != nil {
		t.Fatal(err)
	}

	// The own viewer gets frame seq 2 (skipping JSON status messages).
	for {
		typ, data, err := own.Read(ctx)
		if err != nil {
			t.Fatalf("own viewer read: %v", err)
		}
		if typ == websocket.MessageBinary {
			if binary.LittleEndian.Uint32(data[8:12]) != 5 || binary.LittleEndian.Uint32(data[12:16]) != 2 {
				t.Fatalf("own viewer got stream %d seq %d", binary.LittleEndian.Uint32(data[8:12]), binary.LittleEndian.Uint32(data[12:16]))
			}
			break
		}
	}

	// The victim viewer must never get a frame.
	rctx, rcancel := context.WithTimeout(ctx, 300*time.Millisecond)
	defer rcancel()
	for {
		typ, _, err := victim.Read(rctx)
		if err != nil {
			break // timeout: nothing delivered
		}
		if typ == websocket.MessageBinary {
			t.Fatal("frame for stream 6 reached its viewer from PC 5's agent")
		}
	}
}

func TestGatewayAuthAgentUnknownCertRejected(t *testing.T) {
	der := testCertDER(t)
	g := NewGatewayAuthenticator("secret", "iss", "aud", false, loopbackOnly, staticPCs{})
	if _, _, err := g.AuthAgent(proxiedRequest("127.0.0.1:1", "SUCCESS", nginxEscaped(der))); !errors.Is(err, ErrUnknownAgent) {
		t.Fatalf("unenrolled cert: got %v", err)
	}
	g = NewGatewayAuthenticator("secret", "iss", "aud", false, loopbackOnly, nil)
	if _, _, err := g.AuthAgent(proxiedRequest("127.0.0.1:1", "SUCCESS", nginxEscaped(der))); err == nil {
		t.Fatal("no resolver outside dev mode must reject")
	}
}
