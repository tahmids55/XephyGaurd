package main

import (
	"encoding/binary"
	"testing"
)

func frame(payloads ...[]byte) []byte {
	b := make([]byte, headerSize)
	binary.LittleEndian.PutUint16(b[24:], uint16(len(payloads)))
	for _, p := range payloads {
		h := make([]byte, 10)
		binary.LittleEndian.PutUint32(h[6:], uint32(len(p)))
		b = append(append(b, h...), p...)
	}
	return b
}

func TestBadTiles(t *testing.T) {
	webp := append([]byte("RIFF\x10\x00\x00\x00WEBP"), 1, 2)
	jpeg := []byte{0xFF, 0xD8, 0xFF}
	if bad, ok := badTiles(frame(webp, jpeg)); bad != 0 || !ok {
		t.Fatalf("valid tiles: bad=%d ok=%v", bad, ok)
	}
	if bad, ok := badTiles(frame(webp, []byte("junk"))); bad != 1 || !ok {
		t.Fatalf("junk tile: bad=%d ok=%v", bad, ok)
	}
	trunc := frame(webp)
	if _, ok := badTiles(trunc[:len(trunc)-1]); ok {
		t.Fatal("truncated frame accepted")
	}
	if _, ok := badTiles(frame(webp)[:headerSize+5]); ok {
		t.Fatal("truncated tile header accepted")
	}
}

func TestAckAndPercentile(t *testing.T) {
	a := ack(7, 99)
	if len(a) != headerSize || a[3] != 3 || binary.LittleEndian.Uint32(a[8:]) != 7 || binary.LittleEndian.Uint32(a[12:]) != 99 {
		t.Fatalf("bad ack %v", a)
	}
	v := []float64{5, 1, 3, 2, 4, 10, 9, 8, 7, 6}
	if p := percentile(v, 0.5); p != 6 {
		t.Fatalf("p50 = %v", p)
	}
	if p := percentile(v, 0.95); p != 10 {
		t.Fatalf("p95 = %v", p)
	}
	if percentile(nil, 0.5) != 0 {
		t.Fatal("empty percentile")
	}
}
