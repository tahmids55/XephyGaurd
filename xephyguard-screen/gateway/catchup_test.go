package main

import (
	"encoding/binary"
	"testing"
	"time"

	"github.com/coder/websocket"
)

func deltaFrame(seq uint32) []byte {
	f := makeTestFrame(1, 0, seq)
	binary.LittleEndian.PutUint16(f[4:6], 0) // not KEY
	return f
}

func countKeyReqs(st *Stream) int {
	n := 0
	for {
		select {
		case m := <-st.agentOut:
			if m[3] == kMsgTypeKeyReq {
				n++
			}
		default:
			return n
		}
	}
}

func TestKeyframeRequestsRateLimitedToOnePerSecond(t *testing.T) {
	st := newStream(1)
	st.agentConn = &websocket.Conn{} // only checked for nil
	now := time.Now()
	st.mu.Lock()
	sent := 0
	for i := 0; i < 5; i++ {
		if st.requestKeyframeLocked(now.Add(time.Duration(i)*100*time.Millisecond), 0) {
			sent++
		}
	}
	st.mu.Unlock()
	if sent != 1 || countKeyReqs(st) != 1 {
		t.Fatalf("burst of 5: %d sent now, want 1", sent)
	}
	// The held request goes out when the interval ends (one, not four).
	time.Sleep(kKeyframeReqInterval + 200*time.Millisecond)
	if n := countKeyReqs(st); n != 1 {
		t.Fatalf("held requests after the interval: %d, want 1", n)
	}
}

func TestLaggingViewerSkipsDeltasUntilTheNextKeyframe(t *testing.T) {
	st := newStream(1)
	st.agentConn = &websocket.Conn{}
	slow := &Viewer{id: 1, ch: make(chan []byte, 2), ctrlCh: make(chan []byte, 1)}
	fast := &Viewer{id: 2, ch: make(chan []byte, 64), ctrlCh: make(chan []byte, 1)}
	st.viewers[1], st.viewers[2] = slow, fast
	now := time.Now()
	st.mu.Lock()
	defer st.mu.Unlock()

	st.fanOutLocked(makeTestFrame(1, 0, 1), now)
	st.fanOutLocked(deltaFrame(2), now)
	over, reqs := st.fanOutLocked(deltaFrame(3), now) // slow queue full
	if over != 1 || reqs != 1 || !slow.resync || len(slow.ch) != 0 {
		t.Fatalf("overflow: over=%d reqs=%d resync=%v queued=%d", over, reqs, slow.resync, len(slow.ch))
	}
	st.fanOutLocked(deltaFrame(4), now.Add(10*time.Millisecond))
	if len(slow.ch) != 0 {
		t.Fatal("a resyncing viewer got a delta it cannot apply")
	}
	masked := deltaFrame(5)
	binary.LittleEndian.PutUint16(masked[4:6], kFlagMasked)
	st.fanOutLocked(masked, now)
	if len(slow.ch) != 1 {
		t.Fatal("masked placeholder must reach a resyncing viewer")
	}
	<-slow.ch
	st.fanOutLocked(makeTestFrame(1, 0, 6), now)
	if slow.resync || len(slow.ch) != 1 {
		t.Fatal("keyframe did not end the resync")
	}
	st.fanOutLocked(deltaFrame(7), now)
	if len(slow.ch) != 2 {
		t.Fatal("deltas after the keyframe must flow again")
	}
	if len(fast.ch) != 7 {
		t.Fatalf("fast viewer got %d frames, want all 7", len(fast.ch))
	}
}
