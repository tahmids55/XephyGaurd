package main

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"
)

func TestMetricsCountsFramesAndHidesIdentities(t *testing.T) {
	relay := NewRelayServer(&FakeAuth{})
	srv := httptest.NewServer(relay.Routes())
	defer srv.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	agent, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/agent")+"?stream_id=77", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer agent.Close(websocket.StatusNormalClosure, "")
	viewer, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/view")+"?stream_id=77", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer viewer.Close(websocket.StatusNormalClosure, "")
	time.Sleep(100 * time.Millisecond)

	frame := makeTestFrame(77, 0, 1)
	if err := agent.Write(ctx, websocket.MessageBinary, frame); err != nil {
		t.Fatal(err)
	}
	for {
		typ, _, err := viewer.Read(ctx)
		if err != nil {
			t.Fatal(err)
		}
		if typ == websocket.MessageBinary {
			break
		}
	}

	resp, err := http.Get(srv.URL + "/metrics")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	text := string(body)
	for _, want := range []string{
		"xg_screen_streams 1\n", "xg_screen_agents_connected 1\n", "xg_screen_viewers 1\n",
		"xg_screen_agent_connects_total 1\n", "xg_screen_viewer_connects_total 1\n",
		"xg_screen_frames_in_total 1\n", "xg_screen_frames_out_total 1\n",
	} {
		if !strings.Contains(text, want) {
			t.Errorf("metrics missing %q in:\n%s", want, text)
		}
	}
	if strings.Contains(text, "stream_id") || strings.Contains(text, "pc_id") || strings.Contains(text, "{") {
		t.Error("metrics must be aggregate only, without stream or PC labels")
	}
}

func TestMetricsRefusesNonLoopbackAndPublicPath(t *testing.T) {
	relay := NewRelayServer(&FakeAuth{})
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/metrics", nil)
	req.RemoteAddr = "10.0.0.5:4000"
	relay.Routes().ServeHTTP(rec, req)
	if rec.Code != http.StatusForbidden {
		t.Fatalf("non-loopback got %d, want 403", rec.Code)
	}

	rec = httptest.NewRecorder()
	req = httptest.NewRequest(http.MethodGet, "/screen-gw/metrics", nil)
	req.RemoteAddr = "127.0.0.1:4000"
	relay.Routes().ServeHTTP(rec, req)
	if rec.Code != http.StatusNotFound {
		t.Fatalf("/screen-gw/metrics got %d, want 404", rec.Code)
	}
}
