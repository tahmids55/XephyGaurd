package main

import (
	"testing"
	"time"
)

func TestSweepIdleStreams(t *testing.T) {
	s := NewRelayServer(nil)
	s.getOrCreateStream(1)
	busy := s.getOrCreateStream(2)
	busy.mu.Lock()
	busy.viewers[1] = &Viewer{id: 1}
	busy.mu.Unlock()

	now := time.Now()
	if n := s.sweepIdleStreams(now); n != 0 {
		t.Fatalf("first sweep only starts the idle clock, removed %d", n)
	}
	if n := s.sweepIdleStreams(now.Add(kStreamIdleTTL - time.Second)); n != 0 {
		t.Fatalf("removed before the TTL: %d", n)
	}
	if n := s.sweepIdleStreams(now.Add(kStreamIdleTTL + time.Second)); n != 1 {
		t.Fatalf("idle stream not removed: %d", n)
	}
	if _, ok := s.streams[2]; !ok {
		t.Fatal("stream with a viewer was removed")
	}
	if _, ok := s.streams[1]; ok {
		t.Fatal("idle stream still present")
	}
}
