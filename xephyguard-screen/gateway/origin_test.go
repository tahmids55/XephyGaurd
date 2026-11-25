package main

import (
	"context"
	"net/http"
	"net/http/httptest"
	"net/url"
	"testing"
	"time"

	"github.com/coder/websocket"
)

func dialWithOrigin(ctx context.Context, wsURL, origin string) (*http.Response, error) {
	h := http.Header{}
	if origin != "" {
		h.Set("Origin", origin)
	}
	c, resp, err := websocket.Dial(ctx, wsURL, &websocket.DialOptions{HTTPHeader: h})
	if c != nil {
		c.Close(websocket.StatusNormalClosure, "")
	}
	return resp, err
}

func TestViewerOriginCheck(t *testing.T) {
	relay := NewRelayServer(&FakeAuth{})
	relay.originPatterns = []string{"dashboard.lab:8000"}
	srv := httptest.NewServer(relay.Routes())
	defer srv.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	u, _ := url.Parse(srv.URL)
	view := toWSURL(srv.URL, "/view") + "?stream_id=90"

	for _, origin := range []string{"http://" + u.Host, "http://dashboard.lab:8000", ""} {
		if _, err := dialWithOrigin(ctx, view, origin); err != nil {
			t.Errorf("origin %q refused: %v", origin, err)
		}
	}
	for _, origin := range []string{"http://evil.example", "http://dashboard.lab:9000", "null"} {
		resp, err := dialWithOrigin(ctx, view, origin)
		if err == nil || resp == nil || resp.StatusCode != http.StatusForbidden {
			t.Errorf("origin %q: want 403, got %v %v", origin, resp, err)
		}
	}
	// A browser page on another site cannot open the agent path either.
	resp, err := dialWithOrigin(ctx, toWSURL(srv.URL, "/agent")+"?stream_id=90", "http://evil.example")
	if err == nil || resp == nil || resp.StatusCode != http.StatusForbidden {
		t.Errorf("agent path with foreign origin: want 403, got %v %v", resp, err)
	}
}

func TestSplitList(t *testing.T) {
	got := splitList(" a:1, ,b ,")
	if len(got) != 2 || got[0] != "a:1" || got[1] != "b" {
		t.Fatalf("got %q", got)
	}
}
