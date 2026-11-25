package main

import (
	"fmt"
	"net/http"
	"net/netip"
	"sync/atomic"
)

// gatewayMetrics are aggregate counters only: no PC, user or stream identity (docs/ETHICS.md).
type gatewayMetrics struct {
	agentConnects  atomic.Uint64
	viewerConnects atomic.Uint64
	framesIn       atomic.Uint64 // frames received from agents
	bytesIn        atomic.Uint64
	framesOut      atomic.Uint64 // frames written to viewers
	bytesOut       atomic.Uint64
	viewerDrops    atomic.Uint64 // frames dropped because a viewer queue was full
	keyframeReqs   atomic.Uint64 // keyframe requests the gateway sent after such a drop
}

// HandleMetrics answers GET /metrics in Prometheus text format. Loopback only; the nginx front proxies
// /screen-gw/ only, so this path is never public (screen Step 21).
func (s *RelayServer) HandleMetrics(w http.ResponseWriter, r *http.Request) {
	ap, err := netip.ParseAddrPort(r.RemoteAddr)
	if err != nil || !ap.Addr().Unmap().IsLoopback() {
		http.Error(w, "forbidden", http.StatusForbidden)
		return
	}
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	streams, agents, viewers := 0, 0, 0
	s.mu.RLock()
	for _, st := range s.streams {
		st.mu.Lock()
		streams++
		if st.agentConn != nil {
			agents++
		}
		viewers += len(st.viewers)
		st.mu.Unlock()
	}
	s.mu.RUnlock()

	m := &s.metrics
	w.Header().Set("Content-Type", "text/plain; version=0.0.4")
	row := func(name, kind, help string, v any) {
		fmt.Fprintf(w, "# HELP %s %s\n# TYPE %s %s\n%s %v\n", name, help, name, kind, name, v)
	}
	row("xg_screen_streams", "gauge", "Streams known to the gateway.", streams)
	row("xg_screen_agents_connected", "gauge", "Streams with a connected agent.", agents)
	row("xg_screen_viewers", "gauge", "Open viewer connections.", viewers)
	row("xg_screen_agent_connects_total", "counter", "Agent connections accepted.", m.agentConnects.Load())
	row("xg_screen_viewer_connects_total", "counter", "Viewer connections accepted.", m.viewerConnects.Load())
	row("xg_screen_frames_in_total", "counter", "Frames received from agents.", m.framesIn.Load())
	row("xg_screen_bytes_in_total", "counter", "Frame bytes received from agents.", m.bytesIn.Load())
	row("xg_screen_frames_out_total", "counter", "Frames written to viewers.", m.framesOut.Load())
	row("xg_screen_bytes_out_total", "counter", "Frame bytes written to viewers.", m.bytesOut.Load())
	row("xg_screen_viewer_drops_total", "counter", "Frames dropped because a viewer queue was full.", m.viewerDrops.Load())
	row("xg_screen_keyframe_requests_total", "counter", "Keyframe requests sent after a viewer queue overflow.", m.keyframeReqs.Load())
}
