package main

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"log"
	"net/http"
	"time"

	"github.com/coder/websocket"
)

func (s *RelayServer) HandleView(w http.ResponseWriter, r *http.Request) {
	// A browser cannot set headers on a WebSocket, so its lease token arrives as the first message (screen Step 40):
	// no token on the request means "accept the socket, then wait for {"type":"auth"}". A token in the URL query
	// is ignored. An Authorization header (non-browser clients) and dev-insecure grants authenticate at upgrade.
	grant, err := s.auth.AuthViewer(r)
	var c *websocket.Conn
	if errors.Is(err, ErrMissingToken) {
		if c, err = websocket.Accept(w, r, &websocket.AcceptOptions{OriginPatterns: s.originPatterns}); err != nil {
			return
		}
		defer c.Close(websocket.StatusInternalError, "viewer closed")
		if grant, err = s.readViewerAuth(c); err != nil {
			log.Printf("[Gateway Viewer] first-message auth refused remote=%s err=%v", r.RemoteAddr, err)
			c.Close(websocket.StatusPolicyViolation, "unauthorized")
			return
		}
	} else if err != nil {
		http.Error(w, "Unauthorized viewer: "+err.Error(), http.StatusUnauthorized)
		return
	}
	if s.revoked.blocks(grant) {
		if c != nil {
			c.Close(websocket.StatusPolicyViolation, "lease revoked")
		} else {
			http.Error(w, "Unauthorized viewer: lease revoked", http.StatusUnauthorized)
		}
		return
	}
	streamID := grant.StreamID

	stream := s.findStreamForViewer(streamID)

	stream.mu.Lock()
	hasAgent := stream.agentConn != nil
	log.Printf("[Gateway Viewer Connect] remote=%s target_stream_id=%d mode=%s has_agent=%v keyframe_cached=%v",
		r.RemoteAddr, streamID, grant.Mode, hasAgent, len(stream.lastKeyframe) > 0)
	if len(stream.viewers) >= kMaxViewers {
		stream.mu.Unlock()
		if c != nil {
			c.Close(websocket.StatusTryAgainLater, "max viewers reached")
		} else {
			http.Error(w, "Max viewers reached (max 2)", http.StatusTooManyRequests)
		}
		return
	}

	s.metrics.viewerConnects.Add(1)
	stream.nextViewerID++
	viewerID := stream.nextViewerID
	v := &Viewer{
		id:     viewerID,
		mode:   grant.Mode,
		grant:  grant,
		ch:     make(chan []byte, kViewerQueueSize),
		ctrlCh: make(chan []byte, 4),
	}
	v.leaseExp.Store(grant.Exp.UnixNano())
	stream.viewers[viewerID] = v

	// Only replay a cached keyframe and ask for a keyframe when a live agent is connected;
	// otherwise keep the 150ms offline status grace timer active.
	// Until it has a keyframe that the following deltas build on, a new viewer gets no delta (screen Step 25).
	v.resync = true
	if hasAgent {
		if cached := stream.cachedKeyframeLocked(time.Now()); cached != nil {
			select {
			case v.ch <- cached:
				// The cached keyframe is the newest frame: the next delta builds on it, no new keyframe needed.
				v.resync = binary.LittleEndian.Uint32(cached[12:16]) != stream.lastFrameSeq
			default:
			}
		}

		if stream.lastMonitors != nil { // the monitor list for the selector, best effort
			select {
			case v.ch <- stream.lastMonitors:
			default:
			}
		}

		// Tell the agent the viewer count and mode first (it picks wall/snapshot/live from it), then ask for a
		// keyframe (rate-limited to 1/s per stream) unless the cache already gave a current picture.
		stream.sendViewerStatusLocked()
		if v.resync {
			stream.requestKeyframeLocked(time.Now(), 0)
		}
	}
	stream.mu.Unlock()

	if c == nil {
		// Origin must be the request host (nginx passes the dashboard host) or match -allowed-origins (screen Step 7).
		c, err = websocket.Accept(w, r, &websocket.AcceptOptions{OriginPatterns: s.originPatterns})
		if err != nil {
			stream.mu.Lock()
			delete(stream.viewers, viewerID)
			stream.sendViewerStatusLocked()
			stream.mu.Unlock()
			return
		}
		defer c.Close(websocket.StatusInternalError, "viewer closed")
	}
	c.SetReadLimit(kViewerReadLimit) // viewers send only 28-byte controls and a small renew message
	stream.mu.Lock()
	v.conn = c // read under stream.mu by closeViewers
	stream.mu.Unlock()

	ctx, cancel := context.WithCancel(r.Context())
	defer cancel()

	go keepAlive(ctx, c)

	// Writer goroutine to viewer
	writerDone := make(chan struct{})
	go func() {
		defer close(writerDone)
		// The socket closes when the lease ends unless the viewer renewed it in band (screen Step 5).
		leaseTimer := time.NewTimer(time.Until(time.Unix(0, v.leaseExp.Load())))
		defer leaseTimer.Stop()
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
			case <-leaseTimer.C:
				if left := time.Until(time.Unix(0, v.leaseExp.Load())); left > 0 {
					leaseTimer.Reset(left)
					continue
				}
				c.Close(kCloseLeaseExpired, "lease expired")
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
				if len(msg) >= kHeaderSize && msg[3] == kMsgTypeFrame {
					s.metrics.framesOut.Add(1)
					s.metrics.bytesOut.Add(uint64(len(msg)))
				}
				// A snapshot lease gets one full picture (a keyframe), then the view ends.
				if v.mode == "snapshot" && isKeyframe(msg) {
					c.Close(websocket.StatusNormalClosure, "snapshot delivered")
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

		if msgType == websocket.MessageText {
			if !s.renewViewerLease(v, grant, data) {
				c.Close(websocket.StatusPolicyViolation, "lease renewal refused")
				break
			}
			continue
		}

		if msgType == websocket.MessageBinary {
			out, ok := viewerControlForAgent(data, stream.streamID)
			if !ok {
				continue // only Ack and KeyframeReq reach the agent (screen Step 6)
			}
			if out[3] == kMsgTypeKeyReq {
				stream.mu.Lock()
				stream.requestKeyframeLocked(time.Now(), binary.LittleEndian.Uint16(out[6:8]))
				stream.mu.Unlock()
				continue
			}
			select {
			case stream.agentOut <- out:
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
	stream.sendViewerStatusLocked()
	v.closed.Store(true)
	stream.mu.Unlock()
}

// kCloseLeaseExpired is the WebSocket close code sent when a viewer lease ends without renewal.
const kCloseLeaseExpired websocket.StatusCode = 4001

// renewViewerLease handles a text message from a viewer. Only {"type":"renew","token":"<jwt>"} is
// accepted; the new token must grant the same stream, PC and mode. It extends the lease and sends
// {"type":"lease","expires_at":<unix>}. Anything else returns false (the caller closes the socket).
func (s *RelayServer) renewViewerLease(v *Viewer, orig ViewerGrant, data []byte) bool {
	var msg struct {
		Type  string `json:"type"`
		Token string `json:"token"`
	}
	if len(data) > 4096 || json.Unmarshal(data, &msg) != nil || msg.Type != "renew" {
		return false
	}
	g, err := s.auth.RenewViewer(msg.Token)
	if err != nil || g.StreamID != orig.StreamID || g.PCID != orig.PCID || g.Mode != orig.Mode ||
		g.UserID != orig.UserID || g.AuditID != orig.AuditID || s.revoked.blocks(g) {
		log.Printf("[Gateway Viewer] lease renewal refused stream_id=%d err=%v", orig.StreamID, err)
		return false
	}
	v.leaseExp.Store(g.Exp.UnixNano())
	if ack, err := json.Marshal(struct {
		Type      string `json:"type"`
		ExpiresAt int64  `json:"expires_at"`
	}{"lease", g.Exp.Unix()}); err == nil {
		select {
		case v.ctrlCh <- ack:
		default:
		}
	}
	return true
}

// kViewerAuthTimeout is how long a socket may stay open before its first message carries a valid token.
const kViewerAuthTimeout = 5 * time.Second

// readViewerAuth waits for {"type":"auth","token":"<jwt>"} as the first message and validates the token.
// Anything else (binary, other text, oversize, no message in time) is an error; the caller closes the socket.
func (s *RelayServer) readViewerAuth(c *websocket.Conn) (ViewerGrant, error) {
	c.SetReadLimit(kViewerReadLimit)
	// A timer, not a read deadline: a deadline would drop the connection without a close frame.
	timer := time.AfterFunc(kViewerAuthTimeout, func() { c.Close(websocket.StatusPolicyViolation, "auth timeout") })
	defer timer.Stop()
	msgType, data, err := c.Read(context.Background())
	if err != nil {
		return ViewerGrant{}, err
	}
	var msg struct {
		Type  string `json:"type"`
		Token string `json:"token"`
	}
	if msgType != websocket.MessageText || json.Unmarshal(data, &msg) != nil || msg.Type != "auth" || msg.Token == "" {
		return ViewerGrant{}, ErrMissingToken
	}
	return s.auth.AuthViewerToken(msg.Token)
}

// kViewerReadLimit caps one viewer message (binary control or text renew).
const kViewerReadLimit = 4096

// viewerControlForAgent allows only Ack and KeyframeReq messages of exactly kHeaderSize bytes with
// the right magic and version, and returns a copy whose stream_id is the viewer's own stream.
func viewerControlForAgent(data []byte, streamID uint32) ([]byte, bool) {
	if len(data) != kHeaderSize || binary.LittleEndian.Uint16(data[0:2]) != kMagic || data[2] != kVersion {
		return nil, false
	}
	if data[3] != kMsgTypeAck && data[3] != kMsgTypeKeyReq {
		return nil, false
	}
	out := make([]byte, kHeaderSize)
	copy(out, data)
	binary.LittleEndian.PutUint32(out[8:12], streamID)
	return out, true
}
