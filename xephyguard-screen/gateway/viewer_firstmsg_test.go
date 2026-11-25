package main

import (
	"context"
	"errors"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/coder/websocket"
)

// firstMsgAuth has no token at upgrade, so a viewer must send {"type":"auth","token":...} first (screen Step 40).
func firstMsgAuth() *FakeAuth {
	return &FakeAuth{
		viewerErr: ErrMissingToken,
		tokenFn: func(tok string) (ViewerGrant, error) {
			if tok != "good" {
				return ViewerGrant{}, errors.New("bad token")
			}
			return ViewerGrant{StreamID: 50, PCID: 50, Mode: "live", Exp: time.Now().Add(time.Minute)}, nil
		},
	}
}

func closeCode(t *testing.T, c *websocket.Conn, ctx context.Context) websocket.StatusCode {
	t.Helper()
	for {
		if _, _, err := c.Read(ctx); err != nil {
			var ce websocket.CloseError
			if !errors.As(err, &ce) {
				t.Fatalf("expected a close frame, got %v", err)
			}
			return ce.Code
		}
	}
}

func TestViewerFirstMessageAuth(t *testing.T) {
	srv := httptest.NewServer(NewRelayServer(firstMsgAuth()).Routes())
	defer srv.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	agent, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/agent")+"?stream_id=50", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer agent.Close(websocket.StatusNormalClosure, "")

	// A good token as the first message: the viewer gets frames.
	v, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/view"), nil)
	if err != nil {
		t.Fatalf("upgrade must succeed without a token: %v", err)
	}
	defer v.Close(websocket.StatusNormalClosure, "")
	if err := v.Write(ctx, websocket.MessageText, []byte(`{"type":"auth","token":"good"}`)); err != nil {
		t.Fatal(err)
	}
	time.Sleep(100 * time.Millisecond)
	if err := agent.Write(ctx, websocket.MessageBinary, makeTestFrame(50, 0, 1)); err != nil {
		t.Fatal(err)
	}
	for {
		typ, _, err := v.Read(ctx)
		if err != nil {
			t.Fatalf("no frame after a good auth: %v", err)
		}
		if typ == websocket.MessageBinary {
			break
		}
	}

	// Bad token, wrong type, binary first, and a token in the URL query are all refused with 1008.
	for name, send := range map[string]func(c *websocket.Conn){
		"bad token": func(c *websocket.Conn) { c.Write(ctx, websocket.MessageText, []byte(`{"type":"auth","token":"nope"}`)) },
		"wrong type": func(c *websocket.Conn) {
			c.Write(ctx, websocket.MessageText, []byte(`{"type":"renew","token":"good"}`))
		},
		"binary":     func(c *websocket.Conn) { c.Write(ctx, websocket.MessageBinary, make([]byte, 28)) },
		"query only": func(c *websocket.Conn) {},
	} {
		url := toWSURL(srv.URL, "/view")
		if name == "query only" {
			url += "?token=good"
		}
		c, _, err := websocket.Dial(ctx, url, nil)
		if err != nil {
			t.Fatalf("%s: dial: %v", name, err)
		}
		send(c)
		if name == "query only" {
			// No first message: closed after kViewerAuthTimeout, not authenticated by the URL.
			cctx, ccancel := context.WithTimeout(ctx, kViewerAuthTimeout+3*time.Second)
			if code := closeCode(t, c, cctx); code != websocket.StatusPolicyViolation {
				t.Fatalf("%s: close code %d, want 1008", name, code)
			}
			ccancel()
			continue
		}
		if code := closeCode(t, c, ctx); code != websocket.StatusPolicyViolation {
			t.Fatalf("%s: close code %d, want 1008", name, code)
		}
	}
}
