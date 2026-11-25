// screenbench carries one xgstreambench workload (--stdio) over a real running gateway to a headless viewer and
// reports capture-to-viewer latency p50/p95, received frames and drops (screen Step 20, part b).
// The gateway must run with -dev-insecure. The viewer parses and counts frames, checks that every tile carries a
// WebP or JPEG signature and ACKs like the browser does; it does not decode pixels or paint, so browser decode and
// paint lag stay with the ?debug=1 overlay in a real browser.
package main

import (
	"bufio"
	"context"
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"sort"
	"strconv"
	"sync"
	"time"

	"github.com/coder/websocket"
)

const headerSize = 28

type report struct {
	Workload     string          `json:"workload"`
	Seconds      float64         `json:"seconds"`
	SentFrames   uint64          `json:"sent_frames"`
	ViewerFrames uint64          `json:"viewer_frames"`
	Drops        uint64          `json:"drops"`
	BadTiles     uint64          `json:"bad_tiles"`
	ViewerBytes  float64         `json:"viewer_bytes_per_s"`
	LatencyP50   float64         `json:"latency_ms_p50"`
	LatencyP95   float64         `json:"latency_ms_p95"`
	Streamer     json.RawMessage `json:"streamer,omitempty"`
}

func percentile(v []float64, p float64) float64 {
	if len(v) == 0 {
		return 0
	}
	sort.Float64s(v)
	i := int(float64(len(v)) * p)
	if i >= len(v) {
		i = len(v) - 1
	}
	return v[i]
}

// badTiles counts tiles whose payload is not WebP (RIFF....WEBP) or JPEG (FFD8); ok is false for a malformed frame.
func badTiles(msg []byte) (bad int, ok bool) {
	count := int(binary.LittleEndian.Uint16(msg[24:26]))
	off := headerSize
	for i := 0; i < count; i++ {
		if off+10 > len(msg) {
			return bad, false
		}
		n := int(binary.LittleEndian.Uint32(msg[off+6 : off+10]))
		off += 10
		if off+n > len(msg) {
			return bad, false
		}
		p := msg[off : off+n]
		webp := len(p) >= 12 && string(p[0:4]) == "RIFF" && string(p[8:12]) == "WEBP"
		jpeg := len(p) >= 2 && p[0] == 0xFF && p[1] == 0xD8
		if !webp && !jpeg {
			bad++
		}
		off += n
	}
	return bad, true
}

func ack(stream uint32, seq uint32) []byte {
	b := make([]byte, headerSize)
	binary.LittleEndian.PutUint16(b[0:], 0x5847)
	b[2], b[3] = 1, 3
	binary.LittleEndian.PutUint32(b[8:], stream)
	binary.LittleEndian.PutUint32(b[12:], seq)
	return b
}

func main() {
	gw := flag.String("gateway", "127.0.0.1:8082", "gateway host:port (started with -dev-insecure)")
	stream := flag.Uint("stream", 1, "stream (PC) id")
	bench := flag.String("bench", "build/xgstreambench", "path of xgstreambench")
	workload := flag.String("workload", "typing", "idle|static|typing|scrolling|drag|video")
	seconds := flag.Int("seconds", 10, "seconds to stream")
	capture := flag.String("capture", "synthetic", "synthetic|x11 (x11 needs $DISPLAY, e.g. Xvfb)")
	cpu := flag.String("cpu", "", "pin xgstreambench to this CPU list with taskset -c (the bridge itself stays unpinned)")
	flag.Parse()
	if err := run(*gw, uint32(*stream), *bench, *workload, *seconds, *capture, *cpu); err != nil {
		fmt.Fprintln(os.Stderr, "screenbench:", err)
		os.Exit(1)
	}
}

func run(gw string, stream uint32, bench, workload string, seconds int, capture, cpu string) error {
	ctx, cancel := context.WithTimeout(context.Background(), time.Duration(seconds+30)*time.Second)
	defer cancel()
	q := "?stream_id=" + strconv.Itoa(int(stream))
	viewer, _, err := websocket.Dial(ctx, "ws://"+gw+"/view"+q+"&mode=live", nil)
	if err != nil {
		return fmt.Errorf("viewer connect: %w", err)
	}
	defer viewer.CloseNow()
	agent, _, err := websocket.Dial(ctx, "ws://"+gw+"/agent"+q, nil)
	if err != nil {
		return fmt.Errorf("agent connect: %w", err)
	}
	defer agent.CloseNow()
	agent.SetReadLimit(4096)
	viewer.SetReadLimit(16 << 20)

	args := []string{bench, "--stdio", "--json", "--capture", capture, "--workload", workload, "--seconds", strconv.Itoa(seconds)}
	if cpu != "" {
		args = append([]string{"taskset", "-c", cpu}, args...)
	}
	cmd := exec.CommandContext(ctx, args[0], args[1:]...)
	toBench, _ := cmd.StdinPipe()
	fromBench, _ := cmd.StdoutPipe()
	var row []byte
	errPipe, _ := cmd.StderrPipe()
	if err := cmd.Start(); err != nil {
		return err
	}

	var mu sync.Mutex
	sentAt := map[uint32]int64{} // seq -> capture time (unix ns)
	var sent, got, bad uint64
	var gotBytes int
	var lat []float64

	go func() { // gateway -> bench: ACKs, KeyframeReq, Status
		for {
			_, msg, err := agent.Read(ctx)
			if err != nil {
				return
			}
			var l [4]byte
			binary.LittleEndian.PutUint32(l[:], uint32(len(msg)))
			if _, err := toBench.Write(append(l[:], msg...)); err != nil {
				return
			}
		}
	}()
	viewerDone := make(chan struct{})
	go func() { // headless viewer
		defer close(viewerDone)
		for {
			_, msg, err := viewer.Read(ctx)
			if err != nil {
				return
			}
			now := time.Now().UnixNano()
			if len(msg) < headerSize || msg[3] != 1 {
				continue
			}
			seq := binary.LittleEndian.Uint32(msg[12:16])
			b, ok := badTiles(msg)
			mu.Lock()
			got++
			gotBytes += len(msg)
			bad += uint64(b)
			if !ok {
				bad++
			}
			if t, found := sentAt[seq]; found && msg[4]&64 == 0 { // not a replayed cached keyframe
				lat = append(lat, float64(now-t)/1e6)
				delete(sentAt, seq)
			}
			mu.Unlock()
			_ = viewer.Write(ctx, websocket.MessageBinary, ack(stream, seq))
		}
	}()

	r := bufio.NewReaderSize(fromBench, 1<<20)
	t0 := time.Now()
	for {
		var h [12]byte
		if _, err := io.ReadFull(r, h[:]); err != nil {
			break
		}
		n := binary.LittleEndian.Uint32(h[0:4])
		if n < 8+headerSize || n > 8<<20 {
			return fmt.Errorf("bad record length %d from bench", n)
		}
		wire := make([]byte, n-8)
		if _, err := io.ReadFull(r, wire); err != nil {
			break
		}
		if wire[3] == 1 {
			mu.Lock()
			sent++
			sentAt[binary.LittleEndian.Uint32(wire[12:16])] = int64(binary.LittleEndian.Uint64(h[4:12]))
			mu.Unlock()
		}
		if err := agent.Write(ctx, websocket.MessageBinary, wire); err != nil {
			return fmt.Errorf("agent write: %w", err)
		}
	}
	elapsed := time.Since(t0).Seconds()
	row, _ = io.ReadAll(errPipe) // one JSON row; read to EOF before Wait
	werr := cmd.Wait()
	time.Sleep(300 * time.Millisecond) // frames still in flight in the gateway
	viewer.CloseNow()
	<-viewerDone

	mu.Lock()
	defer mu.Unlock()
	rep := report{Workload: workload, Seconds: elapsed, SentFrames: sent, ViewerFrames: got, BadTiles: bad,
		ViewerBytes: float64(gotBytes) / elapsed, LatencyP50: percentile(lat, 0.5), LatencyP95: percentile(lat, 0.95),
		Streamer: json.RawMessage(row)}
	if sent > got {
		rep.Drops = sent - got
	}
	if len(row) == 0 || !json.Valid(row) {
		rep.Streamer = nil
	}
	out, _ := json.Marshal(rep)
	fmt.Println(string(out))
	return werr
}
