package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/coder/websocket"
)

func TestViewerControlForAgentAllowList(t *testing.T) {
	ack := makeTestAck(999, 2, 7)
	out, ok := viewerControlForAgent(ack, 42)
	if !ok || binary.LittleEndian.Uint32(out[8:12]) != 42 || out[3] != kMsgTypeAck {
		t.Fatalf("ack not forwarded with stream rewritten: %v %x", ok, out)
	}
	if binary.LittleEndian.Uint32(ack[8:12]) != 999 {
		t.Fatal("input buffer modified")
	}
	if _, ok := viewerControlForAgent(buildKeyframeReq(1, 0), 42); !ok {
		t.Fatal("keyframe request refused")
	}

	bad := func(mut func(b []byte) []byte) []byte { return mut(append([]byte(nil), ack...)) }
	cases := map[string][]byte{
		"frame type":  bad(func(b []byte) []byte { b[3] = kMsgTypeFrame; return b }),
		"status type": bad(func(b []byte) []byte { b[3] = kMsgTypeStatus; return b }),
		"cursor type": bad(func(b []byte) []byte { b[3] = 2; return b }),
		"bad magic":   bad(func(b []byte) []byte { b[0] = 0; return b }),
		"bad version": bad(func(b []byte) []byte { b[2] = 9; return b }),
		"too long":    bad(func(b []byte) []byte { return append(b, 0) }),
		"too short":   bad(func(b []byte) []byte { return b[:27] }),
		"frame-sized": makeTestFrame(42, 0, 1),
		"empty":       {},
	}
	for name, msg := range cases {
		if _, ok := viewerControlForAgent(msg, 42); ok {
			t.Errorf("%s forwarded", name)
		}
	}
}

func TestViewerInjectionNeverReachesAgent(t *testing.T) {
	srv := httptest.NewServer(NewRelayServer(&FakeAuth{}).Routes())
	defer srv.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	agentConn, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/agent")+"?stream_id=80", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer agentConn.Close(websocket.StatusNormalClosure, "")
	viewer, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/view")+"?stream_id=80", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer viewer.Close(websocket.StatusNormalClosure, "")

	// A forged frame (as if from the agent) and an Ack for another stream, then a valid Ack.
	_ = viewer.Write(ctx, websocket.MessageBinary, makeTestFrame(80, 0, 5))
	_ = viewer.Write(ctx, websocket.MessageBinary, makeTestAck(81, 0, 6))
	_ = viewer.Write(ctx, websocket.MessageBinary, makeTestAck(80, 0, 7))

	// The agent receives keyframe requests / viewer status and exactly the two Acks, both for stream 80.
	acks := 0
	rctx, rcancel := context.WithTimeout(ctx, 700*time.Millisecond)
	defer rcancel()
	for {
		_, data, err := agentConn.Read(rctx)
		if err != nil {
			break
		}
		if len(data) != kHeaderSize {
			t.Fatalf("agent got a %d-byte message from a viewer", len(data))
		}
		if binary.LittleEndian.Uint32(data[8:12]) != 80 {
			t.Fatalf("agent got a message for stream %d", binary.LittleEndian.Uint32(data[8:12]))
		}
		if data[3] == kMsgTypeAck {
			acks++
		}
		if data[3] == kMsgTypeFrame || bytes.Equal(data, makeTestFrame(80, 0, 5)[:kHeaderSize]) {
			t.Fatal("forged frame forwarded")
		}
	}
	if acks != 2 {
		t.Fatalf("agent got %d acks, want 2", acks)
	}
}

func TestViewerOversizeMessageClosesSocket(t *testing.T) {
	srv := httptest.NewServer(NewRelayServer(&FakeAuth{}).Routes())
	defer srv.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	viewer, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/view")+"?stream_id=82", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer viewer.Close(websocket.StatusNormalClosure, "")
	_ = viewer.Write(ctx, websocket.MessageBinary, make([]byte, kViewerReadLimit+1))
	if code, _ := readUntilClose(ctx, t, viewer); code != websocket.StatusMessageTooBig {
		t.Fatalf("close code %d, want %d", code, websocket.StatusMessageTooBig)
	}
}
