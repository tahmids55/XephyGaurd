package main

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"net/http"
	"sync"
	"sync/atomic"
	"time"

	"github.com/coder/websocket"
)

const (
	kMagic           = uint16(0x5847)
	kVersion         = uint8(1)
	kHeaderSize      = 28
	kMaxViewers      = 2
	kViewerQueueSize = 8
	kMsgTypeFrame    = uint8(1)
	kMsgTypeAck      = uint8(3)
	kMsgTypeKeyReq   = uint8(4)
	kMsgTypeStatus   = uint8(5)
	kMsgTypeMonitors = uint8(6)

	// Frame flags (core/protocol.h Flags).
	kFlagKey    = uint16(1)
	kFlagMasked = uint16(8)
	kFlagSecure = uint16(16)
	kFlagCached = uint16(64) // set by the gateway on a replayed cached keyframe

	// kKeyframeCacheTTL is how long a cached keyframe may be replayed to a new viewer.
	kKeyframeCacheTTL = 5 * time.Second

	// kKeyframeReqInterval: at most one KeyframeReq per stream per interval reaches the agent (screen Step 25).
	kKeyframeReqInterval   = time.Second
	kMonitorSwitchInterval = 500 * time.Millisecond
)

type Viewer struct {
	id                uint64
	mode              string       // lease mode: live, snapshot or wall
	grant             ViewerGrant  // the lease it connected with (identity fields for revoke)
	leaseExp          atomic.Int64 // lease end (unix nanoseconds); extended by an in-band renewal
	ch                chan []byte
	ctrlCh            chan []byte
	conn              *websocket.Conn
	closed            atomic.Bool
	sentOfflineNotice atomic.Bool
	// resync (guarded by the stream's mu): the viewer missed frames or its first picture is older than the stream,
	// so deltas would not apply; it gets no delta until the next keyframe (screen Step 25).
	resync bool
}

type Stream struct {
	streamID       uint32
	agentConn      *websocket.Conn
	agentID        string
	agentOut       chan []byte
	mu             sync.Mutex
	viewers        map[uint64]*Viewer
	nextViewerID   uint64
	lastKeyframe   []byte
	lastMonitorReq time.Time // last monitor switch let through the keyframe rate limit
	lastMonitors   []byte    // newest Monitors message (screen Step 35); replayed to a new viewer, cleared when the agent goes
	lastKeyframeAt time.Time
	lastFrameSeq   uint32      // seq of the newest frame fanned out
	lastKeyReqAt   time.Time   // last KeyframeReq sent to the agent
	keyReqTimer    *time.Timer // a request held back by kKeyframeReqInterval; nil when none
	ctx            context.Context
	cancel         context.CancelFunc
	idleSince      time.Time // guarded by RelayServer.mu: when a sweep first saw no agent and no viewer (zero = busy)
}

func newStream(streamID uint32) *Stream {
	ctx, cancel := context.WithCancel(context.Background())
	return &Stream{
		streamID: streamID,
		agentOut: make(chan []byte, 32),
		viewers:  make(map[uint64]*Viewer),
		ctx:      ctx,
		cancel:   cancel,
	}
}

type RelayServer struct {
	auth    Authenticator
	mu      sync.RWMutex
	streams map[uint32]*Stream
	// internalToken guards POST /internal/revoke; empty disables the endpoint.
	internalToken string
	revoked       *revocations
	metrics       gatewayMetrics
	// originPatterns are extra allowed browser Origin hosts (host[:port], path.Match patterns) besides
	// the request's own host, e.g. the dashboard on another port in a native dev setup.
	originPatterns []string
}

func NewRelayServer(auth Authenticator) *RelayServer {
	return &RelayServer{
		auth:    auth,
		streams: make(map[uint32]*Stream),
		revoked: newRevocations(),
	}
}

func (s *RelayServer) getOrCreateStream(streamID uint32) *Stream {
	s.mu.Lock()
	defer s.mu.Unlock()
	st, exists := s.streams[streamID]
	if !exists {
		st = newStream(streamID)
		s.streams[streamID] = st
	}
	st.idleSince = time.Time{}
	return st
}

func (s *RelayServer) findStreamForViewer(preferredID uint32) *Stream {
	s.mu.Lock()
	defer s.mu.Unlock()

	// Get or create the specific stream for this PC/student
	st, exists := s.streams[preferredID]
	if !exists {
		st = newStream(preferredID)
		s.streams[preferredID] = st
	}
	st.idleSince = time.Time{}
	return st
}

// kStreamIdleTTL is how long a stream with no agent and no viewer stays in the map (screen Step 47).
const kStreamIdleTTL = 60 * time.Second

// sweepIdleStreams removes streams that had neither an agent connection nor a viewer for kStreamIdleTTL.
// A handler that fetches a stream resets its idle clock under s.mu, so a stream cannot be removed from under it.
func (s *RelayServer) sweepIdleStreams(now time.Time) int {
	s.mu.Lock()
	defer s.mu.Unlock()
	removed := 0
	for id, st := range s.streams {
		st.mu.Lock()
		busy := st.agentConn != nil || len(st.viewers) > 0
		st.mu.Unlock()
		switch {
		case busy:
			st.idleSince = time.Time{}
		case st.idleSince.IsZero():
			st.idleSince = now
		case now.Sub(st.idleSince) >= kStreamIdleTTL:
			st.cancel()
			delete(s.streams, id)
			removed++
		}
	}
	return removed
}

// RunJanitor sweeps idle streams every 15 s until ctx ends.
func (s *RelayServer) RunJanitor(ctx context.Context) {
	t := time.NewTicker(15 * time.Second)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case now := <-t.C:
			s.sweepIdleStreams(now)
		}
	}
}

// keepAlive pings the peer every 10 s (answer within 10 s) and closes the socket when it does not answer, so a
// half-dead connection ends instead of lingering (screen Step 44). Needs a concurrent reader on c.
func keepAlive(ctx context.Context, c *websocket.Conn) {
	t := time.NewTicker(10 * time.Second)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			pctx, cancel := context.WithTimeout(ctx, 10*time.Second)
			err := c.Ping(pctx)
			cancel()
			if err != nil {
				if ctx.Err() == nil {
					c.Close(websocket.StatusGoingAway, "ping timeout")
				}
				return
			}
		}
	}
}

func (s *RelayServer) removeStream(streamID uint32) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if st, exists := s.streams[streamID]; exists {
		st.cancel()
		delete(s.streams, streamID)
	}
}

// Build a binary KeyframeReq message according to protocol.h
func buildKeyframeReq(streamID uint32, monitorID uint16) []byte {
	buf := make([]byte, kHeaderSize)
	binary.LittleEndian.PutUint16(buf[0:2], kMagic)
	buf[2] = kVersion
	buf[3] = kMsgTypeKeyReq
	binary.LittleEndian.PutUint16(buf[4:6], 0)
	binary.LittleEndian.PutUint16(buf[6:8], monitorID)
	binary.LittleEndian.PutUint32(buf[8:12], streamID)
	return buf
}

// View mode codes carried in the Status message's reserved header field (docs/PROTOCOL.md). 0 = no viewer / not
// stated; an agent treats 0 with viewers > 0 as live.
const (
	kModeNone     = uint16(0)
	kModeLive     = uint16(1)
	kModeSnapshot = uint16(2)
	kModeWall     = uint16(3)
)

func modeCode(mode string) uint16 {
	switch mode {
	case "live":
		return kModeLive
	case "snapshot":
		return kModeSnapshot
	case "wall":
		return kModeWall
	}
	return kModeNone
}

// viewerModeLocked is the mode the agent must serve for the current viewers: the most demanding one wins
// (live > snapshot > wall). Caller holds s.mu.
func (s *Stream) viewerModeLocked() uint16 {
	best := kModeNone
	for _, v := range s.viewers {
		switch c := modeCode(v.mode); {
		case c == kModeLive:
			return kModeLive
		case c == kModeSnapshot:
			best = kModeSnapshot
		case c == kModeWall && best == kModeNone:
			best = kModeWall
		}
	}
	return best
}

// sendViewerStatusLocked tells the agent how many viewers there are and in which mode, so it can start,
// switch or stop capture. Caller holds s.mu. A full agent queue drops it (the next change sends another).
func (s *Stream) sendViewerStatusLocked() {
	if s.agentConn == nil {
		return
	}
	select {
	case s.agentOut <- buildViewerStatus(s.streamID, uint16(len(s.viewers)), s.viewerModeLocked()):
	default:
	}
}

// Build a binary Status message reporting active viewer count and view mode to the agent.
func buildViewerStatus(streamID uint32, viewers uint16, mode uint16) []byte {
	buf := make([]byte, kHeaderSize)
	binary.LittleEndian.PutUint16(buf[0:2], kMagic)
	buf[2] = kVersion
	buf[3] = kMsgTypeStatus
	binary.LittleEndian.PutUint16(buf[4:6], viewers)
	binary.LittleEndian.PutUint16(buf[6:8], 0)
	binary.LittleEndian.PutUint32(buf[8:12], streamID)
	binary.LittleEndian.PutUint16(buf[20:22], viewers)
	binary.LittleEndian.PutUint16(buf[26:28], mode)
	return buf
}

func (s *RelayServer) broadcastStatusLocked(stream *Stream, connected bool) {
	if len(stream.viewers) == 0 {
		return
	}
	payload, err := json.Marshal(struct {
		Type           string `json:"type"`
		AgentConnected bool   `json:"agent_connected"`
		StreamID       uint32 `json:"stream_id"`
	}{
		Type:           "status",
		AgentConnected: connected,
		StreamID:       stream.streamID,
	})
	if err != nil {
		return
	}
	for _, v := range stream.viewers {
		if connected {
			if !v.sentOfflineNotice.Swap(false) {
				continue
			}
		} else {
			v.sentOfflineNotice.Store(true)
		}
		select {
		case v.ctrlCh <- payload:
		default:
		}
	}
}

func (s *RelayServer) HandleHealth(w http.ResponseWriter, r *http.Request) {
	s.mu.RLock()
	active := 0
	for _, st := range s.streams {
		st.mu.Lock()
		if st.agentConn != nil {
			active++
		}
		st.mu.Unlock()
	}
	s.mu.RUnlock()

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	_ = json.NewEncoder(w).Encode(map[string]any{
		"ok":             true,
		"active_streams": active,
	})
}

func (s *RelayServer) Routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/agent", s.HandleAgent)
	mux.HandleFunc("/screen-gw/agent", s.HandleAgent)
	mux.HandleFunc("/view", s.HandleView)
	mux.HandleFunc("/screen-gw/view", s.HandleView)
	mux.HandleFunc("/health", s.HandleHealth)
	mux.HandleFunc("/screen-gw/health", s.HandleHealth)
	mux.HandleFunc("/metrics", s.HandleMetrics)        // loopback only and not under /screen-gw/, so nginx never serves it
	mux.HandleFunc("/internal/revoke", s.HandleRevoke) // loopback only; nginx never proxies /internal/
	return mux
}

// isKeyframe reports whether msg is a frame message with the KEY flag set.
func isKeyframe(msg []byte) bool {
	return len(msg) >= kHeaderSize &&
		binary.LittleEndian.Uint16(msg[0:2]) == kMagic &&
		msg[3] == kMsgTypeFrame &&
		binary.LittleEndian.Uint16(msg[4:6])&1 != 0
}

// cachedKeyframeLocked returns a copy of the cached keyframe flagged CACHED, or nil when there is
// none or it is older than kKeyframeCacheTTL. Caller holds st.mu.
func (st *Stream) cachedKeyframeLocked(now time.Time) []byte {
	if len(st.lastKeyframe) < kHeaderSize || now.Sub(st.lastKeyframeAt) > kKeyframeCacheTTL {
		st.lastKeyframe = nil
		return nil
	}
	out := make([]byte, len(st.lastKeyframe))
	copy(out, st.lastKeyframe)
	binary.LittleEndian.PutUint16(out[4:6], binary.LittleEndian.Uint16(out[4:6])|kFlagCached)
	return out
}

// requestKeyframeLocked sends a KeyframeReq to the agent at most once per kKeyframeReqInterval. A request inside the
// interval is held and sent when it ends (one timer, so a burst of requests becomes one). Returns true when one was
// queued for the agent now. Caller holds st.mu.
func (st *Stream) requestKeyframeLocked(now time.Time, monitorID uint16) bool {
	if st.agentConn == nil {
		return false
	}
	// A monitor switch (monitorID != 0, user-initiated) is not held back by the 1/s limit, but at most 2 per second.
	switchNow := monitorID != 0 && now.Sub(st.lastMonitorReq) >= kMonitorSwitchInterval
	if switchNow {
		st.lastMonitorReq = now
	}
	if wait := st.lastKeyReqAt.Add(kKeyframeReqInterval).Sub(now); !switchNow && !st.lastKeyReqAt.IsZero() && wait > 0 {
		if st.keyReqTimer == nil {
			st.keyReqTimer = time.AfterFunc(wait, func() {
				st.mu.Lock()
				defer st.mu.Unlock()
				st.keyReqTimer = nil
				st.requestKeyframeLocked(time.Now(), monitorID)
			})
		}
		return false
	}
	st.lastKeyReqAt = now
	select {
	case st.agentOut <- buildKeyframeReq(st.streamID, monitorID):
		return true
	default:
		return false
	}
}

// fanOutLocked delivers one agent message to every viewer. A viewer whose queue is full is caught up instead of
// fed a broken delta chain: its queue is emptied, it waits for the next keyframe (a keyframe that overflows it is
// queued at once), and the agent is asked for one, rate-limited. Masked/secure placeholders always go through.
// Returns the number of viewers that overflowed. Caller holds st.mu.
func (st *Stream) fanOutLocked(data []byte, now time.Time) (overflows int, keyReqs int) {
	isFrame := data[3] == kMsgTypeFrame
	flags := binary.LittleEndian.Uint16(data[4:6])
	isKey := isFrame && flags&kFlagKey != 0
	isDelta := isFrame && !isKey && flags&(kFlagMasked|kFlagSecure) == 0
	if isFrame {
		st.lastFrameSeq = binary.LittleEndian.Uint32(data[12:16])
	}
	for _, v := range st.viewers {
		if v.resync && isDelta {
			continue
		}
		if !isFrame { // cursor position etc.: best effort, never empties a viewer's queue or breaks its delta chain
			select {
			case v.ch <- data:
			default:
			}
			continue
		}
		select {
		case v.ch <- data:
			if isKey {
				v.resync = false
			}
			continue
		default:
		}
		overflows++
		for drained := false; !drained; {
			select {
			case <-v.ch:
			default:
				drained = true
			}
		}
		if isKey || !isDelta {
			select {
			case v.ch <- data:
			default:
			}
		}
		if isKey {
			v.resync = false
			continue
		}
		v.resync = true
		if st.requestKeyframeLocked(now, binary.LittleEndian.Uint16(data[6:8])) {
			keyReqs++
		}
	}
	return overflows, keyReqs
}
