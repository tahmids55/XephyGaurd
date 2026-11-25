package main

import (
	"context"
	"encoding/binary"
	"log"
	"net/http"
	"strconv"
	"time"

	"github.com/coder/websocket"
)

func (s *RelayServer) HandleAgent(w http.ResponseWriter, r *http.Request) {
	agentID, pcID, err := s.auth.AuthAgent(r)
	if err != nil || pcID == 0 {
		log.Printf("[Gateway Agent Rejected] remote=%s path=%s err=%v", r.RemoteAddr, r.URL.Path, err)
		http.Error(w, "Unauthorized agent", http.StatusUnauthorized)
		return
	}

	// The agent streams only as its own PC (screen Step 3). A ?stream_id naming another PC is refused.
	if sidStr := r.URL.Query().Get("stream_id"); sidStr != "" {
		parsed, perr := strconv.ParseUint(sidStr, 10, 32)
		if perr != nil || uint32(parsed) != pcID {
			log.Printf("[Gateway Agent Rejected] remote=%s pc_id=%d asked for stream_id=%q", r.RemoteAddr, pcID, sidStr)
			http.Error(w, "stream_id does not match this agent's PC", http.StatusForbidden)
			return
		}
	}

	c, err := websocket.Accept(w, r, &websocket.AcceptOptions{OriginPatterns: s.originPatterns})
	if err != nil {
		return
	}
	c.SetReadLimit(16 * 1024 * 1024)
	defer c.Close(websocket.StatusInternalError, "agent closed")

	log.Printf("[Gateway Agent Connected] remote=%s path=%s pc_id=%d", r.RemoteAddr, r.URL.Path, pcID)

	// The stream is fixed for the whole connection and resolved before the writer starts.
	s.metrics.agentConnects.Add(1)
	stream := s.getOrCreateStream(pcID)
	stream.mu.Lock()
	if stream.agentConn != c {
		stream.lastKeyframe = nil
		stream.lastMonitors = nil
	}
	stream.agentConn = c
	stream.agentID = agentID
	stream.lastKeyReqAt = time.Time{} // a new agent connection gets its first request at once
	for _, v := range stream.viewers {
		v.resync = true // the new connection starts a new frame chain
	}
	if len(stream.viewers) > 0 {
		s.broadcastStatusLocked(stream, true)
		stream.sendViewerStatusLocked()
		stream.requestKeyframeLocked(time.Now(), 0)
	}
	stream.mu.Unlock()

	ctx, cancel := context.WithCancel(r.Context())
	defer cancel()

	go keepAlive(ctx, c)

	// Writer loop to agent for ACKs and Keyframe requests
	agentWriterDone := make(chan struct{})
	go func() {
		defer close(agentWriterDone)
		for {
			var out []byte
			select {
			case <-ctx.Done():
				return
			case out = <-stream.agentOut:
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

			if frameStreamID != stream.streamID {
				// A frame for another PC's stream: drop it (screen Step 3).
				continue
			}

			stream.mu.Lock()
			if stream.agentConn != c {
				// Another connection of this PC took over; this one no longer feeds viewers.
				stream.mu.Unlock()
				continue
			}

			// Cache the last keyframe for a fast first picture (screen Step 9). A masked or
			// secure-desktop frame clears it, so a cached picture never outlives the privacy state.
			if data[3] == kMsgTypeFrame {
				switch {
				case flags&(kFlagMasked|kFlagSecure) != 0:
					stream.lastKeyframe = nil
				case flags&kFlagKey != 0:
					log.Printf("[Gateway Agent Keyframe] from remote=%s stream_id=%d size=%d", r.RemoteAddr, frameStreamID, len(data))
					stream.lastKeyframe = make([]byte, len(data))
					copy(stream.lastKeyframe, data)
					stream.lastKeyframeAt = time.Now()
				}
			}

			if data[3] == kMsgTypeMonitors {
				stream.lastMonitors = append([]byte(nil), data...)
			}

			if data[3] == kMsgTypeFrame {
				s.metrics.framesIn.Add(1)
				s.metrics.bytesIn.Add(uint64(len(data)))
			}

			overflows, keyReqs := stream.fanOutLocked(data, time.Now())
			s.metrics.viewerDrops.Add(uint64(overflows))
			s.metrics.keyframeReqs.Add(uint64(keyReqs))
			stream.mu.Unlock()
		}
	}

	cancel()
	<-agentWriterDone

	stream.mu.Lock()
	if stream.agentConn == c {
		stream.agentConn = nil
		stream.lastKeyframe = nil
		stream.lastMonitors = nil
		s.broadcastStatusLocked(stream, false)
	}
	stream.mu.Unlock()
}
