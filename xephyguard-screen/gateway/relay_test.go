package main

import (
	"context"
	"encoding/binary"
	"errors"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strconv"
	"testing"
	"time"

	"github.com/coder/websocket"
)

type FakeAuth struct {
	agentErr   error
	viewerErr  error
	agentID    string
	streamIDFn func(r *http.Request) (uint32, error)
}

func (f *FakeAuth) AuthViewer(r *http.Request) (uint32, error) {
	if f.viewerErr != nil {
		return 0, f.viewerErr
	}
	if f.streamIDFn != nil {
		return f.streamIDFn(r)
	}
	sidStr := r.URL.Query().Get("stream_id")
	if sidStr != "" {
		sid, _ := strconv.ParseUint(sidStr, 10, 32)
		return uint32(sid), nil
	}
	return 100, nil
}

func (f *FakeAuth) AuthAgent(r *http.Request) (string, error) {
	if f.agentErr != nil {
		return "", f.agentErr
	}
	if f.agentID != "" {
		return f.agentID, nil
	}
	return "test-agent-fp", nil
}

func makeTestFrame(streamID uint32, monitorID uint16, seq uint32) []byte {
	return makeTestFrameWithPayload(streamID, monitorID, seq, 0)
}

func makeTestFrameWithPayload(streamID uint32, monitorID uint16, seq uint32, payloadLen int) []byte {
	buf := make([]byte, 28+10+payloadLen)
	binary.LittleEndian.PutUint16(buf[0:2], kMagic)
	buf[2] = kVersion
	buf[3] = kMsgTypeFrame
	binary.LittleEndian.PutUint16(buf[4:6], 1) // KEY
	binary.LittleEndian.PutUint16(buf[6:8], monitorID)
	binary.LittleEndian.PutUint32(buf[8:12], streamID)
	binary.LittleEndian.PutUint32(buf[12:16], seq)
	binary.LittleEndian.PutUint32(buf[16:20], 0) // base_seq
	binary.LittleEndian.PutUint16(buf[20:22], 64)
	binary.LittleEndian.PutUint16(buf[22:24], 64)
	binary.LittleEndian.PutUint16(buf[24:26], 1) // tile_count
	binary.LittleEndian.PutUint32(buf[34:38], uint32(payloadLen))
	return buf
}

func makeTestAck(streamID uint32, monitorID uint16, seq uint32) []byte {
	buf := make([]byte, 28)
	binary.LittleEndian.PutUint16(buf[0:2], kMagic)
	buf[2] = kVersion
	buf[3] = 3 // ACK
	binary.LittleEndian.PutUint16(buf[6:8], monitorID)
	binary.LittleEndian.PutUint32(buf[8:12], streamID)
	binary.LittleEndian.PutUint32(buf[12:16], seq)
	return buf
}

func toWSURL(httpURL string, path string) string {
	u, _ := url.Parse(httpURL)
	u.Scheme = "ws"
	u.Path = path
	return u.String()
}

func TestPairingAndForwardingBothDirections(t *testing.T) {
	auth := &FakeAuth{}
	relay := NewRelayServer(auth)
	srv := httptest.NewServer(relay.Routes())
	defer srv.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	// 1. Connect agent
	agentWSURL := toWSURL(srv.URL, "/agent") + "?stream_id=100"
	agentConn, _, err := websocket.Dial(ctx, agentWSURL, nil)
	if err != nil {
		t.Fatalf("agent dial failed: %v", err)
	}
	defer agentConn.Close(websocket.StatusNormalClosure, "")

	// 2. Connect viewer
	viewWSURL := toWSURL(srv.URL, "/view") + "?stream_id=100"
	viewerConn, _, err := websocket.Dial(ctx, viewWSURL, nil)
	if err != nil {
		t.Fatalf("viewer dial failed: %v", err)
	}
	defer viewerConn.Close(websocket.StatusNormalClosure, "")

	// Give a moment for pairing
	time.Sleep(50 * time.Millisecond)

	// 3. Agent sends frame -> viewer must receive it
	frame := makeTestFrame(100, 1, 1)
	if err := agentConn.Write(ctx, websocket.MessageBinary, frame); err != nil {
		t.Fatalf("agent write failed: %v", err)
	}

	_, rcvd, err := viewerConn.Read(ctx)
	if err != nil {
		t.Fatalf("viewer read failed: %v", err)
	}
	if len(rcvd) != len(frame) {
		t.Fatalf("received length %d != sent length %d", len(rcvd), len(frame))
	}

	// 4. Viewer sends ACK -> agent must receive it
	ack := makeTestAck(100, 1, 1)
	if err := viewerConn.Write(ctx, websocket.MessageBinary, ack); err != nil {
		t.Fatalf("viewer write ACK failed: %v", err)
	}

	_, agentRcvd, err := agentConn.Read(ctx)
	if err != nil {
		t.Fatalf("agent read ACK failed: %v", err)
	}
	if len(agentRcvd) < 28 || agentRcvd[3] != 3 {
		t.Fatalf("agent did not receive ACK message: %v", agentRcvd)
	}
}

func TestThirdViewerRefused(t *testing.T) {
	auth := &FakeAuth{}
	relay := NewRelayServer(auth)
	srv := httptest.NewServer(relay.Routes())
	defer srv.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	agentWSURL := toWSURL(srv.URL, "/agent") + "?stream_id=200"
	agentConn, _, err := websocket.Dial(ctx, agentWSURL, nil)
	if err != nil {
		t.Fatalf("agent dial failed: %v", err)
	}
	defer agentConn.Close(websocket.StatusNormalClosure, "")

	viewWSURL := toWSURL(srv.URL, "/view") + "?stream_id=200"

	// Viewer 1
	v1, _, err := websocket.Dial(ctx, viewWSURL, nil)
	if err != nil {
		t.Fatalf("viewer 1 dial failed: %v", err)
	}
	defer v1.Close(websocket.StatusNormalClosure, "")

	// Viewer 2
	v2, _, err := websocket.Dial(ctx, viewWSURL, nil)
	if err != nil {
		t.Fatalf("viewer 2 dial failed: %v", err)
	}
	defer v2.Close(websocket.StatusNormalClosure, "")

	// Viewer 3: must be refused (HTTP 429)
	_, resp, err := websocket.Dial(ctx, viewWSURL, nil)
	if err == nil {
		t.Fatalf("viewer 3 expected failure but succeeded")
	}
	if resp != nil && resp.StatusCode != http.StatusTooManyRequests {
		t.Fatalf("expected status 429 Too Many Requests, got %d", resp.StatusCode)
	}
}

func TestQueueOverflowTriggersKeyframeReq(t *testing.T) {
	auth := &FakeAuth{}
	relay := NewRelayServer(auth)
	srv := httptest.NewServer(relay.Routes())
	defer srv.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	agentWSURL := toWSURL(srv.URL, "/agent") + "?stream_id=300"
	agentConn, _, err := websocket.Dial(ctx, agentWSURL, nil)
	if err != nil {
		t.Fatalf("agent dial failed: %v", err)
	}
	agentConn.SetReadLimit(16 * 1024 * 1024)
	defer agentConn.Close(websocket.StatusNormalClosure, "")

	viewWSURL := toWSURL(srv.URL, "/view") + "?stream_id=300"
	viewerConn, _, err := websocket.Dial(ctx, viewWSURL, nil)
	if err != nil {
		t.Fatalf("viewer dial failed: %v", err)
	}
	viewerConn.SetReadLimit(16 * 1024 * 1024)
	defer viewerConn.Close(websocket.StatusNormalClosure, "")

	time.Sleep(50 * time.Millisecond)

	// Send large frames to viewer without viewer reading.
	// 64KB per frame fills loopback TCP buffer quickly, causing v.ch (size 8) to overflow.
	for i := uint32(1); i <= 16; i++ {
		frame := makeTestFrameWithPayload(300, 1, i, 65536)
		_ = agentConn.Write(ctx, websocket.MessageBinary, frame)
	}

	// Agent should receive a KEYFRAME_REQ generated by the relay
	gotKeyReq := false
	for i := 0; i < 5; i++ {
		readCtx, readCancel := context.WithTimeout(ctx, 1*time.Second)
		_, data, err := agentConn.Read(readCtx)
		readCancel()
		if err != nil {
			t.Logf("agent read attempt %d returned error: %v", i, err)
		} else {
			t.Logf("agent read attempt %d received msg type: %d, len: %d", i, data[3], len(data))
			if len(data) >= 28 && data[3] == kMsgTypeKeyReq {
				gotKeyReq = true
				break
			}
		}
	}

	if !gotKeyReq {
		t.Fatalf("expected agent to receive KEYFRAME_REQ upon viewer queue overflow")
	}
}

func TestAuthRejections(t *testing.T) {
	auth := &FakeAuth{
		agentErr:  errors.New("bad agent cert"),
		viewerErr: errors.New("bad jwt"),
	}
	relay := NewRelayServer(auth)
	srv := httptest.NewServer(relay.Routes())
	defer srv.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	_, respAgent, errAgent := websocket.Dial(ctx, toWSURL(srv.URL, "/agent"), nil)
	if errAgent == nil || respAgent == nil || respAgent.StatusCode != http.StatusUnauthorized {
		t.Fatalf("expected 401 for agent, got %v", respAgent)
	}

	_, respView, errView := websocket.Dial(ctx, toWSURL(srv.URL, "/view"), nil)
	if errView == nil || respView == nil || respView.StatusCode != http.StatusUnauthorized {
		t.Fatalf("expected 401 for viewer, got %v", respView)
	}
}

func TestDisconnectCleanup(t *testing.T) {
	auth := &FakeAuth{}
	relay := NewRelayServer(auth)
	srv := httptest.NewServer(relay.Routes())
	defer srv.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	agentConn, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/agent")+"?stream_id=500", nil)
	if err != nil {
		t.Fatalf("agent dial failed: %v", err)
	}

	v1, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/view")+"?stream_id=500", nil)
	if err != nil {
		t.Fatalf("viewer dial failed: %v", err)
	}

	// Disconnect viewer
	_ = v1.Close(websocket.StatusNormalClosure, "")
	time.Sleep(50 * time.Millisecond)

	// Agent should still be able to send
	_ = agentConn.Write(ctx, websocket.MessageBinary, makeTestFrame(500, 1, 1))

	// Disconnect agent
	_ = agentConn.Close(websocket.StatusNormalClosure, "")
	time.Sleep(50 * time.Millisecond)
}
