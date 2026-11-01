#!/usr/bin/env bash
# XephyGuard agent installer for Debian/Ubuntu lab PCs, served by the CCC.
#   curl -fsSL http://{{ $ccc }}/install/linux.sh | sudo bash -s -- --key <ENROLLMENT KEY>
set -euo pipefail
CCC="{{ $ccc }}"
CA_FP="{{ $fingerprint }}"
KEY=""
while [ $# -gt 0 ]; do case "$1" in --key) KEY="$2"; shift 2;; --ccc) CCC="$2"; shift 2;; *) echo "unknown option $1"; exit 2;; esac; done
[ -n "$KEY" ] || { echo "usage: install.sh --key <enrollment key>"; exit 2; }
[ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
echo "==> Fetching CA certificate and verifying fingerprint"
curl -fsSL "http://$CCC/ca.crt" -o "$tmp/ca.crt"
got=$(openssl x509 -in "$tmp/ca.crt" -noout -fingerprint -sha256 | cut -d= -f2 | tr -d ':' | tr 'A-F' 'a-f')
[ "$got" = "$CA_FP" ] || { echo "CA fingerprint mismatch ($got) - aborting"; exit 3; }
echo "==> Downloading agent package"
arch=$(dpkg --print-architecture)
deb="xephyguard-agent_latest_${arch}.deb"   # scripts/release.sh publishes this alias
curl -fsSL "http://$CCC/downloads/$deb" -o "$tmp/agent.deb"
install -d -m 0755 /etc/xephyguard && install -m 0644 "$tmp/ca.crt" /etc/xephyguard/ca.crt
apt-get install -y "$tmp/agent.deb"
echo "==> Enrolling with the CCC (approve this PC on the dashboard)"
xephyguard-agent enroll --key "$KEY" --ccc "$CCC" --ca-fingerprint "$CA_FP" --timeout 600
systemctl restart xephyguard-agent
echo "==> Done. Status:"; xephyguard-agent status
