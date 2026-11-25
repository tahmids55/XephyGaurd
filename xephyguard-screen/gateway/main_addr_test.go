package main

import "testing"

func TestIsLoopbackAddr(t *testing.T) {
	for addr, want := range map[string]bool{
		"127.0.0.1:8082": true, "[::1]:8082": true, "localhost:8082": true,
		":8082": false, "0.0.0.0:8082": false, "10.0.0.5:8082": false, "garbage": false,
	} {
		if got := isLoopbackAddr(addr); got != want {
			t.Errorf("isLoopbackAddr(%q) = %v, want %v", addr, got, want)
		}
	}
}
