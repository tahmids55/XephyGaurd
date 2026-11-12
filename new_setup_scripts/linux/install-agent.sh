#!/usr/bin/env bash
# XephyGuard Linux Student PC Agent Installer & Enrollee (Self-Contained)
# Works on Ubuntu/Debian student workstations both directly or via remote SSH push.
# Note: Can also be installed directly from a running CCC via:
#   curl -fsSL http://<CCC_IP>/install/linux.sh | sudo bash -s -- --key <ENROLL_KEY>
set -euo pipefail

CCC=""
KEY=""
CA_FP=""
DEB_PATH=""
HTTP_PORT=80
TIMEOUT=600
SKIP_BROWSER_CA=0
BROWSER_CA_URL_PATH=""

usage() {
  cat <<EOF
Usage: $(basename "$0") --ccc <CCC_IP_OR_HOST> --key <ENROLL_KEY> [options]

Required:
  --ccc <IP_OR_HOST>       CCC server IP address or hostname
  --key <ENROLL_KEY>       Current lab enrollment key (shown on CCC dashboard / Enroll page)

Options:
  --cafp <SHA256>          Expected SHA-256 fingerprint of Lab CA (recommended verification)
  --deb <PATH>             Install local .deb package instead of downloading from CCC
  --http-port <PORT>       CCC HTTP download port for CA/.deb (default: 80; use 8000 for native without Nginx)
  --timeout <SECONDS>      Enrollment approval wait timeout (default: 600)
  --skip-browser-ca        Skip installing Lab CA into system/browser NSS stores
  -h, --help               Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ccc) CCC="${2:?Missing value for --ccc}"; shift 2 ;;
    --key) KEY="${2:?Missing value for --key}"; shift 2 ;;
    --cafp) CA_FP="${2:?Missing value for --cafp}"; shift 2 ;;
    --deb) DEB_PATH="${2:?Missing value for --deb}"; shift 2 ;;
    --http-port) HTTP_PORT="${2:?Missing value for --http-port}"; shift 2 ;;
    --timeout) TIMEOUT="${2:?Missing value for --timeout}"; shift 2 ;;
    --skip-browser-ca) SKIP_BROWSER_CA=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "[ERR] Unknown option: $1 (use --help)" >&2; exit 2 ;;
  esac
done

[[ -n "$CCC" ]] || { echo "[ERR] --ccc <CCC_IP_OR_HOST> is required." >&2; usage; exit 2; }
[[ -n "$KEY" ]] || { echo "[ERR] --key <ENROLL_KEY> is required." >&2; usage; exit 2; }
[[ "$(id -u)" -eq 0 ]] || { echo "[ERR] Please run as root (e.g. sudo bash $0 ...)." >&2; exit 1; }

for cmd in curl openssl dpkg apt-get systemctl; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "[ERR] Required tool '$cmd' not found." >&2; exit 1; }
done

BASE_HTTP="http://${CCC}"
[[ "$HTTP_PORT" != "80" ]] && BASE_HTTP="http://${CCC}:${HTTP_PORT}"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

echo "[*] Fetching Lab CA certificate from ${BASE_HTTP}/ca.crt..."
curl -fsSL "${BASE_HTTP}/ca.crt" -o "$TMP_DIR/ca.crt"

ACTUAL_FP="$(openssl x509 -in "$TMP_DIR/ca.crt" -noout -fingerprint -sha256 | cut -d= -f2 | tr -d ':' | tr 'A-F' 'a-f')"
if [[ -n "$CA_FP" ]]; then
  EXPECTED_FP="$(printf '%s' "$CA_FP" | tr -d ': \t\r\n' | tr 'A-F' 'a-f')"
  if [[ "$ACTUAL_FP" != "$EXPECTED_FP" ]]; then
    echo "[ERR] CA fingerprint mismatch! Expected=$EXPECTED_FP Got=$ACTUAL_FP" >&2
    exit 3
  fi
  echo "[OK] CA SHA-256 fingerprint verified ($ACTUAL_FP)."
else
  echo "[*] CA SHA-256 fingerprint: $ACTUAL_FP"
fi

if [[ -n "$DEB_PATH" ]]; then
  [[ -f "$DEB_PATH" ]] || { echo "[ERR] Specified --deb file not found: $DEB_PATH" >&2; exit 1; }
  TARGET_DEB="$(cd -- "$(dirname -- "$DEB_PATH")" && pwd -P)/$(basename -- "$DEB_PATH")"
else
  ARCH="$(dpkg --print-architecture)"
  DEB_NAME="xephyguard-agent_latest_${ARCH}.deb"
  TARGET_DEB="$TMP_DIR/$DEB_NAME"
  echo "[*] Downloading ${BASE_HTTP}/downloads/${DEB_NAME}..."
  if ! curl -fsSL "${BASE_HTTP}/downloads/${DEB_NAME}" -o "$TARGET_DEB"; then
    echo "[ERR] Could not download ${DEB_NAME} from ${BASE_HTTP}/downloads/." >&2
    echo "      Build/upload the .deb or pass --deb /path/to/xephyguard-agent.deb." >&2
    exit 4
  fi
fi

echo "[*] Installing CA certificate into /etc/xephyguard/ca.crt..."
install -d -m 0755 /etc/xephyguard
install -m 0644 "$TMP_DIR/ca.crt" /etc/xephyguard/ca.crt

echo "[*] Installing XephyGuard agent package ($TARGET_DEB)..."
DEBIAN_FRONTEND=noninteractive apt-get install -y "$TARGET_DEB"

# Install CA into system trust store & Firefox/Chrome NSS profiles if repo script is nearby
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_BROWSER_CA="$SCRIPT_DIR/../../installers/linux/install-browser-ca.sh"
if (( SKIP_BROWSER_CA == 0 )); then
  if [[ -f "$REPO_BROWSER_CA" ]]; then
    echo "[*] Installing Lab CA into system & browser trust stores..."
    bash "$REPO_BROWSER_CA" /etc/xephyguard/ca.crt || true
  elif [[ -d /usr/local/share/ca-certificates ]] && command -v update-ca-certificates >/dev/null 2>&1; then
    echo "[*] Trusting Lab CA in system store..."
    install -m 0644 /etc/xephyguard/ca.crt /usr/local/share/ca-certificates/xephyguard-ca.crt
    update-ca-certificates >/dev/null 2>&1 || true
  fi
fi

echo "[*] Enrolling with CCC ($CCC)..."
xephyguard-agent enroll --key "$KEY" --ccc "$CCC" --ca-fingerprint "$ACTUAL_FP" --timeout "$TIMEOUT"

echo "[*] Enabling and starting xephyguard-agent service..."
systemctl enable xephyguard-agent >/dev/null 2>&1 || true
systemctl restart xephyguard-agent

echo "[OK] Installation complete. Agent status:"
xephyguard-agent status || systemctl --no-pager -l status xephyguard-agent
