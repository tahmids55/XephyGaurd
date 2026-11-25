package main

import (
	"context"
	"errors"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/coder/websocket"
)

func TestSnapshotViewerClosedAfterOneKeyframe(t *testing.T) {
	relay := NewRelayServer(&FakeAuth{mode: "snapshot"})
	srv := httptest.NewServer(relay.Routes())
	defer srv.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	agentConn, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/agent")+"?stream_id=40", nil)
	if err != nil {
		t.Fatalf("agent dial: %v", err)
	}
	defer agentConn.Close(websocket.StatusNormalClosure, "")
	viewer, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/view")+"?stream_id=40", nil)
	if err != nil {
		t.Fatalf("viewer dial: %v", err)
	}
	defer viewer.Close(websocket.StatusNormalClosure, "")

	time.Sleep(100 * time.Millisecond) // viewer registered before frames are sent
	for seq := uint32(1); seq <= 3; seq++ {
		if err := agentConn.Write(ctx, websocket.MessageBinary, makeTestFrame(40, 0, seq)); err != nil {
			t.Fatal(err)
		}
	}

	frames := 0
	for {
		typ, _, err := viewer.Read(ctx)
		if err != nil {
			var ce websocket.CloseError
			if !errors.As(err, &ce) || ce.Code != websocket.StatusNormalClosure {
				t.Fatalf("expected a normal close after the snapshot, got %v", err)
			}
			break
		}
		if typ == websocket.MessageBinary {
			frames++
		}
	}
	if frames != 1 {
		t.Fatalf("snapshot viewer got %d frames, want 1", frames)
	}
}
