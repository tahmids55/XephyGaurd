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
