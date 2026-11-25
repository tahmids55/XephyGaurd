package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"
)

var ErrUnknownAgent = errors.New("agent certificate is not enrolled, revoked or its PC is retired")

// PCResolver maps an agent certificate fingerprint to the PC it was issued to (screen Step 3).
type PCResolver interface {
	ResolvePC(ctx context.Context, fingerprint string) (uint32, error)
}

// ServerPCResolver asks the CCC server's internal API
// (GET /api/internal/screen/agent/{fingerprint}, bearer internal token, see docs/PROTOCOL.md).
type ServerPCResolver struct {
	BaseURL string
	Token   string
	Client  *http.Client
}

func NewServerPCResolver(baseURL, token string) *ServerPCResolver {
	return &ServerPCResolver{
		BaseURL: strings.TrimRight(baseURL, "/"),
		Token:   token,
		Client:  &http.Client{Timeout: 3 * time.Second},
	}
}

func isHexFingerprint(fp string) bool {
	if len(fp) != 64 {
		return false
	}
	for _, c := range fp {
		if !(c >= '0' && c <= '9' || c >= 'a' && c <= 'f') {
			return false
		}
	}
	return true
}

func (s *ServerPCResolver) ResolvePC(ctx context.Context, fingerprint string) (uint32, error) {
	if !isHexFingerprint(fingerprint) {
		return 0, ErrUnknownAgent
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, s.BaseURL+"/api/internal/screen/agent/"+fingerprint, nil)
	if err != nil {
		return 0, err
	}
	req.Header.Set("Authorization", "Bearer "+s.Token)
	req.Header.Set("Accept", "application/json")
	resp, err := s.Client.Do(req)
	if err != nil {
		return 0, fmt.Errorf("pc lookup: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusNotFound {
		return 0, ErrUnknownAgent
	}
	if resp.StatusCode != http.StatusOK {
		return 0, fmt.Errorf("pc lookup: server returned %d", resp.StatusCode)
	}
	var body struct {
		PCID uint32 `json:"pc_id"`
	}
	if err := json.NewDecoder(io.LimitReader(resp.Body, 4096)).Decode(&body); err != nil {
		return 0, fmt.Errorf("pc lookup: %w", err)
	}
	if body.PCID == 0 {
		return 0, ErrUnknownAgent
	}
	return body.PCID, nil
}
