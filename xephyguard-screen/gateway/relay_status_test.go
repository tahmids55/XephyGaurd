package main

import (
	"context"
	"encoding/json"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/coder/websocket"
)

type statusMsg struct {
	Type           string `json:"type"`
	AgentConnected bool   `json:"agent_connected"`
	StreamID       uint32 `json:"stream_id"`
}

func TestViewerReceivesAgentStatusTransitionsAndPrefixedRoutesWork(t *testing.T) {
	auth := &FakeAuth{}
	relay := NewRelayServer(auth)
	srv := httptest.NewServer(relay.Routes())
	defer srv.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	// Viewer connects first via /screen-gw/view before any agent has connected.
	viewerConn, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/screen-gw/view")+"?stream_id=10", nil)
	if err != nil {
		t.Fatalf("dial viewer: %v", err)
	}
	defer viewerConn.Close(websocket.StatusNormalClosure, "bye")

	typ, data, err := viewerConn.Read(ctx)
	if err != nil {
		t.Fatalf("read initial status: %v", err)
	}
	if typ != websocket.MessageText {
		t.Fatalf("expected text status frame, got %v", typ)
	}
	var st statusMsg
	if err := json.Unmarshal(data, &st); err != nil {
		t.Fatalf("decode status json: %v", err)
	}
	if st.Type != "status" || st.AgentConnected || st.StreamID != 10 {
		t.Fatalf("unexpected initial status: %+v", st)
	}

	// Agent connects via /screen-gw/agent; viewer must get agent_connected=true status update, then binary frame.
	agentConn, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/screen-gw/agent")+"?stream_id=10", nil)
	if err != nil {
		t.Fatalf("dial agent: %v", err)
	}
	defer agentConn.Close(websocket.StatusNormalClosure, "bye")

	typ, data, err = viewerConn.Read(ctx)
	if err != nil {
		t.Fatalf("read connected status: %v", err)
	}
	if typ != websocket.MessageText {
		t.Fatalf("expected text status frame on agent connect, got %v", typ)
	}
	if err := json.Unmarshal(data, &st); err != nil {
		t.Fatalf("decode connected status json: %v", err)
	}
	if st.Type != "status" || !st.AgentConnected || st.StreamID != 10 {
		t.Fatalf("unexpected connected status: %+v", st)
	}

	frame := makeTestFrame(10, 0, 1)
	if err := agentConn.Write(ctx, websocket.MessageBinary, frame); err != nil {
		t.Fatalf("agent write frame: %v", err)
	}
	typ, got, err := viewerConn.Read(ctx)
	if err != nil {
		t.Fatalf("viewer read frame: %v", err)
	}
	if typ != websocket.MessageBinary || len(got) != len(frame) {
		t.Fatalf("expected binary frame len=%d, got typ=%v len=%d", len(frame), typ, len(got))
	}
}

func TestDisconnectedAgentDoesNotReplayStaleKeyframeToNewViewer(t *testing.T) {
	auth := &FakeAuth{}
	relay := NewRelayServer(auth)
	srv := httptest.NewServer(relay.Routes())
	defer srv.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	// 1. Agent connects, sends a keyframe, and disconnects.
	agentConn, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/agent")+"?stream_id=22", nil)
	if err != nil {
		t.Fatalf("dial agent: %v", err)
	}
	frame := makeTestFrame(22, 0, 1)
	if err := agentConn.Write(ctx, websocket.MessageBinary, frame); err != nil {
		t.Fatalf("agent write frame: %v", err)
	}
	time.Sleep(50 * time.Millisecond)
	_ = agentConn.Close(websocket.StatusNormalClosure, "done")
	time.Sleep(50 * time.Millisecond)

	// 2. Subsequent viewer connects with no live agent; must get offline status, NOT the stale frame.
	viewerConn, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/screen-gw/view")+"?stream_id=22", nil)
	if err != nil {
		t.Fatalf("dial viewer: %v", err)
	}
	defer viewerConn.Close(websocket.StatusNormalClosure, "bye")

	typ, data, err := viewerConn.Read(ctx)
	if err != nil {
		t.Fatalf("viewer read status: %v", err)
	}
	if typ != websocket.MessageText {
		t.Fatalf("expected offline text status instead of stale binary frame, got msgType=%v len=%d", typ, len(data))
	}
	var st statusMsg
	if err := json.Unmarshal(data, &st); err != nil {
		t.Fatalf("decode status json: %v", err)
	}
	if st.Type != "status" || st.AgentConnected || st.StreamID != 22 {
		t.Fatalf("expected agent_connected=false for stream 22, got %+v", st)
	}
}

func TestGatewayHealthEndpointAndZeroViewerIdleStatus(t *testing.T) {
	auth := &FakeAuth{}
	relay := NewRelayServer(auth)
	srv := httptest.NewServer(relay.Routes())
	defer srv.Close()

	for _, p := range []string{"/health", "/screen-gw/health"} {
		resp, err := srv.Client().Get(srv.URL + p)
		if err != nil {
			t.Fatalf("GET %s failed: %v", p, err)
		}
		var doc map[string]any
		err = json.NewDecoder(resp.Body).Decode(&doc)
		resp.Body.Close()
		if err != nil {
			t.Fatalf("decode %s: %v", p, err)
		}
		if resp.StatusCode != 200 || doc["ok"] != true {
			t.Fatalf("unexpected %s response: status=%d doc=%v", p, resp.StatusCode, doc)
		}
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	agentConn, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/agent")+"?stream_id=77", nil)
	if err != nil {
		t.Fatalf("dial agent: %v", err)
	}
	defer agentConn.Close(websocket.StatusNormalClosure, "bye")

	viewerConn, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/view")+"?stream_id=77", nil)
	if err != nil {
		t.Fatalf("dial viewer: %v", err)
	}

	// Viewer join tells the agent the viewer count and mode (Status, screen Step 13), then asks for a keyframe (4)
	_, st1, err := agentConn.Read(ctx)
	if err != nil {
		t.Fatalf("agent read viewer status: %v", err)
	}
	if len(st1) < kHeaderSize || st1[3] != kMsgTypeStatus || st1[4] != 1 || st1[26] != byte(kModeLive) || st1[27] != 0 {
		t.Fatalf("expected Status(1 viewer, live), got %v", st1)
	}
	_, msg1, err := agentConn.Read(ctx)
	if err != nil {
		t.Fatalf("agent read keyframe req: %v", err)
	}
	if len(msg1) < kHeaderSize || msg1[3] != kMsgTypeKeyReq {
		t.Fatalf("expected KeyframeReq (4), got len=%d type=%d", len(msg1), msg1[3])
	}

	// Health reflects 1 active stream
	resp, err := srv.Client().Get(srv.URL + "/health")
	if err != nil {
		t.Fatalf("GET /health failed: %v", err)
	}
	var doc map[string]any
	_ = json.NewDecoder(resp.Body).Decode(&doc)
	resp.Body.Close()
	if int(doc["active_streams"].(float64)) != 1 {
		t.Fatalf("expected active_streams=1, got %v", doc)
	}

	// Closing last viewer must send MsgType Status (5) with flags=0 so agent pauses capture
	_ = viewerConn.Close(websocket.StatusNormalClosure, "done")
	_, msg2, err := agentConn.Read(ctx)
	if err != nil {
		t.Fatalf("agent read viewer=0 status: %v", err)
	}
	if len(msg2) < kHeaderSize || msg2[3] != kMsgTypeStatus || msg2[4] != 0 || msg2[5] != 0 || msg2[26] != 0 || msg2[27] != 0 {
		t.Fatalf("expected Status(0) after viewer disconnect, got len=%d type=%d flags=(%d,%d)", len(msg2), msg2[3], msg2[4], msg2[5])
	}
}

func TestStatusCarriesTheViewerMode(t *testing.T) {
	for _, tc := range []struct {
		mode string
		code uint16
	}{{"wall", kModeWall}, {"snapshot", kModeSnapshot}, {"live", kModeLive}} {
		relay := NewRelayServer(&FakeAuth{mode: tc.mode})
		srv := httptest.NewServer(relay.Routes())
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		agentConn, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/agent")+"?stream_id=78", nil)
		if err != nil {
			t.Fatalf("dial agent: %v", err)
		}
		viewerConn, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/view")+"?stream_id=78", nil)
		if err != nil {
			t.Fatalf("dial viewer: %v", err)
		}
		_, st, err := agentConn.Read(ctx)
		if err != nil {
			t.Fatalf("%s: read status: %v", tc.mode, err)
		}
		got := uint16(st[26]) | uint16(st[27])<<8
		if st[3] != kMsgTypeStatus || got != tc.code {
			t.Fatalf("%s: Status mode = %d, want %d (msg %v)", tc.mode, got, tc.code, st)
		}
		viewerConn.Close(websocket.StatusNormalClosure, "")
		agentConn.Close(websocket.StatusNormalClosure, "")
		cancel()
		srv.Close()
	}
}

func TestViewerModeLockedPicksTheMostDemandingMode(t *testing.T) {
	s := newStream(1)
	if got := s.viewerModeLocked(); got != kModeNone {
		t.Fatalf("no viewers: %d", got)
	}
	s.viewers[1] = &Viewer{mode: "wall"}
	if got := s.viewerModeLocked(); got != kModeWall {
		t.Fatalf("wall only: %d", got)
	}
	s.viewers[2] = &Viewer{mode: "snapshot"}
	if got := s.viewerModeLocked(); got != kModeSnapshot {
		t.Fatalf("wall+snapshot: %d", got)
	}
	s.viewers[3] = &Viewer{mode: "live"}
	if got := s.viewerModeLocked(); got != kModeLive {
		t.Fatalf("with live: %d", got)
	}
	delete(s.viewers, 3)
	delete(s.viewers, 2)
	if got := s.viewerModeLocked(); got != kModeWall {
		t.Fatalf("back to wall: %d", got)
	}
}
