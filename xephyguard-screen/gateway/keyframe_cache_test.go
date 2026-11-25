package main

import (
	"context"
	"encoding/binary"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/coder/websocket"
)

func TestCachedKeyframeTTLAndFlag(t *testing.T) {
	st := newStream(1)
	now := time.Now()
	st.lastKeyframe = makeTestFrame(1, 0, 3)
	st.lastKeyframeAt = now

	got := st.cachedKeyframeLocked(now.Add(time.Second))
	if got == nil || binary.LittleEndian.Uint16(got[4:6]) != kFlagKey|kFlagCached {
		t.Fatalf("fresh cache: got %x", got)
	}
	if binary.LittleEndian.Uint16(st.lastKeyframe[4:6])&kFlagCached != 0 {
		t.Fatal("cache itself was flagged (must return a copy)")
	}
	if st.cachedKeyframeLocked(now.Add(kKeyframeCacheTTL+time.Millisecond)) != nil || st.lastKeyframe != nil {
		t.Fatal("expired cache replayed or kept")
	}
}

// firstFrame dials a viewer on stream 95 and returns the first binary message it gets (nil if none).
func firstFrame(ctx context.Context, t *testing.T, srvURL string) []byte {
	t.Helper()
	v, _, err := websocket.Dial(ctx, toWSURL(srvURL, "/view")+"?stream_id=95", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close(websocket.StatusNormalClosure, "")
	rctx, cancel := context.WithTimeout(ctx, 300*time.Millisecond)
	defer cancel()
	for {
		typ, data, err := v.Read(rctx)
		if err != nil {
			return nil
		}
		if typ == websocket.MessageBinary {
			return data
		}
	}
}

func TestMaskedFrameClearsKeyframeCache(t *testing.T) {
	srv := httptest.NewServer(NewRelayServer(&FakeAuth{}).Routes())
	defer srv.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	agent, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/agent")+"?stream_id=95", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer agent.Close(websocket.StatusNormalClosure, "")

	if err := agent.Write(ctx, websocket.MessageBinary, makeTestFrame(95, 0, 1)); err != nil {
		t.Fatal(err)
	}
	time.Sleep(100 * time.Millisecond)
	got := firstFrame(ctx, t, srv.URL)
	if got == nil || binary.LittleEndian.Uint16(got[4:6])&kFlagCached == 0 {
		t.Fatalf("new viewer did not get the cached keyframe flagged CACHED: %x", got)
	}

	masked := makeTestFrame(95, 0, 2)
	binary.LittleEndian.PutUint16(masked[4:6], kFlagMasked|4) // masked delta
	if err := agent.Write(ctx, websocket.MessageBinary, masked); err != nil {
		t.Fatal(err)
	}
	time.Sleep(100 * time.Millisecond)
	if got := firstFrame(ctx, t, srv.URL); got != nil {
		t.Fatalf("cached picture replayed after a masked frame: %x", got)
	}

	secureKey := makeTestFrame(95, 0, 3)
	binary.LittleEndian.PutUint16(secureKey[4:6], kFlagKey|kFlagSecure)
	if err := agent.Write(ctx, websocket.MessageBinary, secureKey); err != nil {
		t.Fatal(err)
	}
	time.Sleep(100 * time.Millisecond)
	if got := firstFrame(ctx, t, srv.URL); got != nil {
		t.Fatalf("secure-desktop keyframe was cached: %x", got)
	}
}
