package main

import (
	"context"
	"encoding/binary"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/coder/websocket"
)

// monitorsMsg is a header-only Monitors message (type 6) with one 8-byte record.
func monitorsMsg(stream uint32) []byte {
	b := make([]byte, kHeaderSize+8)
	binary.LittleEndian.PutUint16(b[0:2], kMagic)
	b[2] = kVersion
	b[3] = kMsgTypeMonitors
	binary.LittleEndian.PutUint32(b[8:12], stream)
	binary.LittleEndian.PutUint16(b[24:26], 1)
	binary.LittleEndian.PutUint16(b[kHeaderSize+4:], 1920)
	binary.LittleEndian.PutUint16(b[kHeaderSize+6:], 1080)
	return b
}

// A viewer that joins after the agent announced its monitors still gets the list (screen Step 35).
func TestMonitorsMessageReplayedToLateViewer(t *testing.T) {
	srv := httptest.NewServer(NewRelayServer(&FakeAuth{}).Routes())
	defer srv.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	agent, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/agent")+"?stream_id=60", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer agent.Close(websocket.StatusNormalClosure, "")
	if err := agent.Write(ctx, websocket.MessageBinary, monitorsMsg(60)); err != nil {
		t.Fatal(err)
	}
	time.Sleep(100 * time.Millisecond)
	v, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/view")+"?stream_id=60", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close(websocket.StatusNormalClosure, "")
	for {
		typ, data, err := v.Read(ctx)
		if err != nil {
			t.Fatalf("no Monitors message replayed: %v", err)
		}
		if typ == websocket.MessageBinary && len(data) > 3 && data[3] == kMsgTypeMonitors {
			return
		}
	}
}

// A monitor switch is not held back by the 1/s keyframe limit (but 0 = "keep" still is).
func TestMonitorSwitchBypassesKeyframeLimit(t *testing.T) {
	st := newStream(61)
	st.agentConn = &websocket.Conn{} // only checked for nil
	now := time.Now()
	if !st.requestKeyframeLocked(now, 0) {
		t.Fatal("first request must go out")
	}
	if st.requestKeyframeLocked(now.Add(100*time.Millisecond), 0) {
		t.Fatal("second plain request inside 1 s must be held")
	}
	if !st.requestKeyframeLocked(now.Add(200*time.Millisecond), 2) {
		t.Fatal("a monitor switch must go out at once")
	}
	if st.requestKeyframeLocked(now.Add(300*time.Millisecond), 1) {
		t.Fatal("a second switch within 500 ms must be held")
	}
}
