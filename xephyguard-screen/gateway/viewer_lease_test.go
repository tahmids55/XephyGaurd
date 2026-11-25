package main

import (
	"context"
	"errors"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"
)

// readUntilClose reads until the socket closes and returns the close code and the text messages seen.
func readUntilClose(ctx context.Context, t *testing.T, c *websocket.Conn) (websocket.StatusCode, []string) {
	t.Helper()
	var texts []string
	for {
		typ, data, err := c.Read(ctx)
		if err != nil {
			var ce websocket.CloseError
			if errors.As(err, &ce) {
				return ce.Code, texts
			}
			t.Fatalf("read ended without a close frame: %v", err)
		}
		if typ == websocket.MessageText {
			texts = append(texts, string(data))
		}
	}
}

func dialViewer(ctx context.Context, t *testing.T, auth *FakeAuth) *websocket.Conn {
	t.Helper()
	srv := httptest.NewServer(NewRelayServer(auth).Routes())
	t.Cleanup(srv.Close)
	c, _, err := websocket.Dial(ctx, toWSURL(srv.URL, "/view")+"?stream_id=60", nil)
	if err != nil {
		t.Fatalf("viewer dial: %v", err)
	}
	t.Cleanup(func() { c.Close(websocket.StatusNormalClosure, "") })
	return c
}

func TestViewerClosedAtLeaseExpiry(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	c := dialViewer(ctx, t, &FakeAuth{lease: 300 * time.Millisecond})
	start := time.Now()
	code, _ := readUntilClose(ctx, t, c)
	if code != kCloseLeaseExpired {
		t.Fatalf("close code %d, want %d", code, kCloseLeaseExpired)
	}
	if time.Since(start) > 2*time.Second {
		t.Fatalf("closed too late: %v", time.Since(start))
	}
}

func TestViewerInBandRenewalExtendsLease(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	auth := &FakeAuth{lease: 400 * time.Millisecond, renewFn: func(tok string) (ViewerGrant, error) {
		if tok != "fresh" {
			return ViewerGrant{}, errors.New("bad token")
		}
		return ViewerGrant{StreamID: 60, PCID: 60, Mode: "live", Exp: time.Now().Add(900 * time.Millisecond)}, nil
	}}
	c := dialViewer(ctx, t, auth)
	start := time.Now()
	if err := c.Write(ctx, websocket.MessageText, []byte(`{"type":"renew","token":"fresh"}`)); err != nil {
		t.Fatal(err)
	}
	code, texts := readUntilClose(ctx, t, c)
	if code != kCloseLeaseExpired {
		t.Fatalf("close code %d, want %d", code, kCloseLeaseExpired)
	}
	if time.Since(start) < 700*time.Millisecond {
		t.Fatalf("renewed lease ended after %v, before the renewed expiry", time.Since(start))
	}
	acked := false
	for _, m := range texts {
		acked = acked || strings.Contains(m, `"type":"lease"`)
	}
	if !acked {
		t.Fatalf("no lease ack in %q", texts)
	}
}

func TestViewerRenewalRefused(t *testing.T) {
	other := func(string) (ViewerGrant, error) {
		return ViewerGrant{StreamID: 61, PCID: 61, Mode: "live", Exp: time.Now().Add(time.Minute)}, nil
	}
	upgrade := func(string) (ViewerGrant, error) {
		return ViewerGrant{StreamID: 60, PCID: 60, Mode: "wall", Exp: time.Now().Add(time.Minute)}, nil
	}
	cases := []struct {
		name string
		auth *FakeAuth
		msg  string
	}{
		{"invalid token", &FakeAuth{}, `{"type":"renew","token":"x"}`},
		{"other stream", &FakeAuth{renewFn: other}, `{"type":"renew","token":"x"}`},
		{"other mode", &FakeAuth{renewFn: upgrade}, `{"type":"renew","token":"x"}`},
		{"not json", &FakeAuth{renewFn: other}, `hello`},
		{"unknown type", &FakeAuth{renewFn: other}, `{"type":"control"}`},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
			defer cancel()
			c := dialViewer(ctx, t, tc.auth)
			if err := c.Write(ctx, websocket.MessageText, []byte(tc.msg)); err != nil {
				t.Fatal(err)
			}
			if code, _ := readUntilClose(ctx, t, c); code != websocket.StatusPolicyViolation {
				t.Fatalf("close code %d, want %d", code, websocket.StatusPolicyViolation)
			}
		})
	}
}
