package main

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"log"
	"net/http"
	"strconv"
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
	kMsgTypeKeyReq   = uint8(4)
)

type Viewer struct {
	id                uint64
	ch                chan []byte
	ctrlCh            chan []byte
	conn              *websocket.Conn
	closed            atomic.Bool
	sentOfflineNotice atomic.Bool
}

type Stream struct {
	streamID     uint32
	agentConn    *websocket.Conn
	agentID      string
	agentOut     chan []byte
	mu           sync.Mutex
	viewers      map[uint64]*Viewer
	nextViewerID uint64
	lastKeyframe []byte
	ctx          context.Context
	cancel       context.CancelFunc
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
}

func NewRelayServer(auth Authenticator) *RelayServer {
	return &RelayServer{
		auth:    auth,
		streams: make(map[uint32]*Stream),
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
	return st
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

func (s *RelayServer) HandleAgent(w http.ResponseWriter, r *http.Request) {
	agentID, err := s.auth.AuthAgent(r)
	if err != nil {
		log.Printf("[Gateway Agent Rejected] remote=%s path=%s err=%v", r.RemoteAddr, r.URL.Path, err)
		http.Error(w, "Unauthorized agent: "+err.Error(), http.StatusUnauthorized)
		return
	}

	streamIDVal := uint32(0)
	if sidStr := r.URL.Query().Get("stream_id"); sidStr != "" {
		if parsed, err := strconv.ParseUint(sidStr, 10, 32); err == nil {
			streamIDVal = uint32(parsed)
		}
	}

	c, err := websocket.Accept(w, r, &websocket.AcceptOptions{
		InsecureSkipVerify: true,
	})
	if err != nil {
		return
	}
	c.SetReadLimit(16 * 1024 * 1024)
	defer c.Close(websocket.StatusInternalError, "agent closed")

	log.Printf("[Gateway Agent Connected] remote=%s path=%s stream_id_param=%d", r.RemoteAddr, r.URL.Path, streamIDVal)

	var stream *Stream
	if streamIDVal != 0 {
		stream = s.getOrCreateStream(streamIDVal)
		stream.mu.Lock()
		stream.agentConn = c
		stream.agentID = agentID
		if len(stream.viewers) > 0 {
			s.broadcastStatusLocked(stream, true)
			select {
			case stream.agentOut <- buildKeyframeReq(stream.streamID, 0):
			default:
			}
		}
		stream.mu.Unlock()
	}

	ctx, cancel := context.WithCancel(r.Context())
	defer cancel()

	// Writer loop to agent for ACKs and Keyframe requests
	agentWriterDone := make(chan struct{})
	go func() {
		defer close(agentWriterDone)
		for {
			var out []byte
			if stream != nil {
				select {
				case <-ctx.Done():
					return
				case out = <-stream.agentOut:
				}
			} else {
				select {
				case <-ctx.Done():
					return
				case <-time.After(50 * time.Millisecond):
					continue
				}
			}

			writeCtx, writeCancel := context.WithTimeout(ctx, 3*time.Second)
			err := c.Write(writeCtx, websocket.MessageBinary, out)
			writeCancel()
			if err != nil {
				return
			}
		}
	}()

	// Reader loop from agent
	for {
		msgType, data, err := c.Read(ctx)
		if err != nil {
			break
		}

		if msgType == websocket.MessageText {
			// Forward control messages or handle control ping/command
			continue
		}

		if msgType == websocket.MessageBinary && len(data) >= kHeaderSize {
			magic := binary.LittleEndian.Uint16(data[0:2])
			if magic != kMagic {
				continue
			}

			frameStreamID := binary.LittleEndian.Uint32(data[8:12])
			flags := binary.LittleEndian.Uint16(data[4:6])
			monitorID := binary.LittleEndian.Uint16(data[6:8])

			if stream == nil || stream.streamID != frameStreamID {
				stream = s.getOrCreateStream(frameStreamID)
			}

			stream.mu.Lock()
			stream.agentConn = c
			stream.agentID = agentID

			// Cache keyframe for instant display on new viewer connections
			if (flags & 1) != 0 {
				log.Printf("[Gateway Agent Keyframe] from remote=%s stream_id=%d size=%d", r.RemoteAddr, frameStreamID, len(data))
				stream.lastKeyframe = make([]byte, len(data))
				copy(stream.lastKeyframe, data)
			}

			// Fan out to viewers
			for _, v := range stream.viewers {
				select {
				case v.ch <- data:
				default:
					// Queue full: drop oldest frame
					select {
					case <-v.ch:
					default:
					}
					// Queue newest
					select {
					case v.ch <- data:
					default:
					}
					// Send KEYFRAME_REQ to agent
					keyReq := buildKeyframeReq(frameStreamID, monitorID)
					select {
					case stream.agentOut <- keyReq:
					default:
					}
				}
			}
			stream.mu.Unlock()
		}
	}

	cancel()
	<-agentWriterDone

	if stream != nil {
		stream.mu.Lock()
		if stream.agentConn == c {
			stream.agentConn = nil
			s.broadcastStatusLocked(stream, false)
		}
		stream.mu.Unlock()
	}
}

func (s *RelayServer) HandleView(w http.ResponseWriter, r *http.Request) {
	streamID, err := s.auth.AuthViewer(r)
	if err != nil {
		http.Error(w, "Unauthorized viewer: "+err.Error(), http.StatusUnauthorized)
		return
	}

	stream := s.findStreamForViewer(streamID)

	stream.mu.Lock()
	hasAgent := stream.agentConn != nil
	log.Printf("[Gateway Viewer Connect] remote=%s target_stream_id=%d has_agent=%v keyframe_cached=%v",
		r.RemoteAddr, streamID, hasAgent, len(stream.lastKeyframe) > 0)
	if len(stream.viewers) >= kMaxViewers {
		stream.mu.Unlock()
		http.Error(w, "Max viewers reached (max 2)", http.StatusTooManyRequests)
		return
	}

	stream.nextViewerID++
	viewerID := stream.nextViewerID
	v := &Viewer{
		id:     viewerID,
		ch:     make(chan []byte, kViewerQueueSize),
		ctrlCh: make(chan []byte, 4),
	}
	stream.viewers[viewerID] = v

	// Defer notifying offline for a brief grace window so concurrent agent/viewer handshakes do not race.
	// If we have a cached keyframe, immediately push it to the new viewer.
	if len(stream.lastKeyframe) > 0 {
		select {
		case v.ch <- stream.lastKeyframe:
		default:
		}
	}

	// Always trigger an immediate keyframe request to the agent
	select {
	case stream.agentOut <- buildKeyframeReq(stream.streamID, 0):
	default:
	}
	stream.mu.Unlock()

	c, err := websocket.Accept(w, r, &websocket.AcceptOptions{
		InsecureSkipVerify: true,
	})
	if err != nil {
		stream.mu.Lock()
		delete(stream.viewers, viewerID)
		stream.mu.Unlock()
		return
	}
	c.SetReadLimit(16 * 1024 * 1024)
	v.conn = c
	defer c.Close(websocket.StatusInternalError, "viewer closed")

	ctx, cancel := context.WithCancel(r.Context())
	defer cancel()

	// Writer goroutine to viewer
	writerDone := make(chan struct{})
	go func() {
		defer close(writerDone)
		var offlineTimer <-chan time.Time
		if !hasAgent {
			t := time.NewTimer(150 * time.Millisecond)
			defer t.Stop()
			offlineTimer = t.C
		}
		for {
			select {
			case <-ctx.Done():
				return
			case <-stream.ctx.Done():
				return
			case <-offlineTimer:
				offlineTimer = nil
				stream.mu.Lock()
				stillOffline := stream.agentConn == nil
				stream.mu.Unlock()
				if stillOffline {
					if statusPayload, merr := json.Marshal(struct {
						Type           string `json:"type"`
						AgentConnected bool   `json:"agent_connected"`
						StreamID       uint32 `json:"stream_id"`
					}{Type: "status", AgentConnected: false, StreamID: stream.streamID}); merr == nil {
						v.sentOfflineNotice.Store(true)
						writeCtx, writeCancel := context.WithTimeout(ctx, 3*time.Second)
						err := c.Write(writeCtx, websocket.MessageText, statusPayload)
						writeCancel()
						if err != nil {
							return
						}
					}
				}
			case ctrlMsg, ok := <-v.ctrlCh:
				if !ok {
					return
				}
				offlineTimer = nil
				writeCtx, writeCancel := context.WithTimeout(ctx, 3*time.Second)
				err := c.Write(writeCtx, websocket.MessageText, ctrlMsg)
				writeCancel()
				if err != nil {
					return
				}
			case msg, ok := <-v.ch:
				if !ok {
					return
				}
				offlineTimer = nil
				writeCtx, writeCancel := context.WithTimeout(ctx, 3*time.Second)
				err := c.Write(writeCtx, websocket.MessageBinary, msg)
				writeCancel()
				if err != nil {
					return
				}
			}
		}
	}()

	// Reader goroutine from viewer: forwards ACKs and KEYFRAME_REQs to agent
	for {
		msgType, data, err := c.Read(ctx)
		if err != nil {
			break
		}

		if msgType == websocket.MessageBinary && len(data) >= kHeaderSize {
			select {
			case stream.agentOut <- data:
			case <-ctx.Done():
				break
			case <-stream.ctx.Done():
				break
			default:
				// Avoid blocking viewer reader if agent queue full
			}
		}
	}

	cancel()
	<-writerDone

	stream.mu.Lock()
	delete(stream.viewers, viewerID)
	v.closed.Store(true)
	stream.mu.Unlock()
}

func (s *RelayServer) Routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/agent", s.HandleAgent)
	mux.HandleFunc("/screen-gw/agent", s.HandleAgent)
	mux.HandleFunc("/view", s.HandleView)
	mux.HandleFunc("/screen-gw/view", s.HandleView)
	return mux
}
