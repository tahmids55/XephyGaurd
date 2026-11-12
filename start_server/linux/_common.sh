#!/usr/bin/env bash
# Shared helper functions for XephyGuard Linux start/stop/status/setup scripts.
# Intended to be sourced, not executed directly.

if [[ -n "${_XG_COMMON_SH_LOADED:-}" ]]; then
  return 0
fi
_XG_COMMON_SH_LOADED=1

_xg_common_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="${REPO_ROOT:-$(cd -- "$_xg_common_dir/../.." && pwd -P)}"
SERVER_DIR="$REPO_ROOT/server"
DEPLOY_DIR="$REPO_ROOT/ccc-deploy"
SCREEN_GW_DIR="$REPO_ROOT/xephyguard-screen/gateway"
PID_DIR="$SERVER_DIR/storage/app/pids"
LOG_DIR="$SERVER_DIR/storage/logs"

if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
  _C_CYAN='\033[1;36m'
  _C_GREEN='\033[0;32m'
  _C_YELLOW='\033[0;33m'
  _C_RED='\033[0;31m'
  _C_DIM='\033[0;90m'
  _C_RESET='\033[0m'
else
  _C_CYAN='' _C_GREEN='' _C_YELLOW='' _C_RED='' _C_DIM='' _C_RESET=''
fi

xg_info() { printf "${_C_CYAN}[*]${_C_RESET} %s\n" "$*"; }
xg_ok()   { printf "${_C_GREEN}[OK]${_C_RESET} %s\n" "$*"; }
xg_warn() { printf "${_C_YELLOW}[!]${_C_RESET} %s\n" "$*" >&2; }
xg_err()  { printf "${_C_RED}[ERR]${_C_RESET} %s\n" "$*" >&2; }
xg_die()  { xg_err "$*"; exit 1; }

xg_rand_hex() {
  local bytes="${1:-32}"
  if command -v openssl >/dev/null 2>&1; then
    openssl rand -hex "$bytes"
  elif command -v python3 >/dev/null 2>&1; then
    python3 -c "import secrets; print(secrets.token_hex($bytes))"
  else
    od -An -tx1 -N"$bytes" /dev/urandom | tr -d ' \n'
    echo
  fi
}

# Read a KEY=VALUE from an env file (handles CRLF and surrounding quotes).
xg_read_env() {
  local file="$1" key="$2" def="${3:-}"
  [[ -f "$file" ]] || { printf '%s' "$def"; return 0; }
  local line
  line="$(tr -d '\r' < "$file" | grep -E "^${key}=" | tail -n1 || true)"
  if [[ -z "$line" ]]; then
    printf '%s' "$def"
    return 0
  fi
  local val="${line#*=}"
  val="${val%\"}"
  val="${val#\"}"
  val="${val%\'}"
  val="${val#\'}"
  if [[ -z "$val" ]]; then
    printf '%s' "$def"
  else
    printf '%s' "$val"
  fi
}

# Set or append KEY=VALUE in an env file cleanly (preserves other lines).
xg_set_env() {
  local file="$1" key="$2" val="$3"
  mkdir -p "$(dirname "$file")"
  touch "$file"
  local tmp
  tmp="$(mktemp)"
  tr -d '\r' < "$file" | awk -v k="$key" -v v="$val" '
    BEGIN { found = 0 }
    index($0, k "=") == 1 {
      print k "=" v
      found = 1
      next
    }
    { print }
    END {
      if (!found) print k "=" v
    }
  ' > "$tmp"
  mv "$tmp" "$file"
}

_XG_VIRT_IF_RE='^(lo|docker.*|br-.*|veth.*|virbr.*|tun.*|tap.*|wg.*|tailscale.*|CloudflareWARP|warp.*|utun.*)$'

xg_detect_lan_iface() {
  local iface="" dev
  if command -v ip >/dev/null 2>&1; then
    while read -r dev; do
      [[ -z "$dev" || "$dev" =~ $_XG_VIRT_IF_RE ]] && continue
      iface="$dev"
      break
    done < <(ip -4 route show default 2>/dev/null | awk '{for(i=1;i<=NF;i++) if($i=="dev"){print $(i+1)}}')

    if [[ -z "$iface" ]]; then
      while read -r dev; do
        [[ -z "$dev" || "$dev" =~ $_XG_VIRT_IF_RE ]] && continue
        iface="$dev"
        break
      done < <(ip -4 -o addr show scope global 2>/dev/null | awk '{print $2}')
    fi

    if [[ -z "$iface" ]]; then
      iface="$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for(i=1;i<=NF;i++) if($i=="dev"){print $(i+1); exit}}' || true)"
    fi
  fi
  printf '%s' "${iface:-eth0}"
}

xg_detect_lan_ip() {
  local ip="" iface=""
  iface="$(xg_detect_lan_iface)"
  if command -v ip >/dev/null 2>&1; then
    if [[ -n "$iface" && ! "$iface" =~ $_XG_VIRT_IF_RE ]]; then
      ip="$(ip -4 -o addr show dev "$iface" scope global 2>/dev/null | awk '{split($4,a,"/"); print a[1]; exit}' || true)"
    fi
    if [[ -z "$ip" ]]; then
      ip="$(ip -4 -o addr show scope global 2>/dev/null | awk -v vre="$_XG_VIRT_IF_RE" '$2 !~ vre {split($4,a,"/"); if(a[1] !~ /^(127\.|169\.254\.)/){print a[1]; exit}}' || true)"
    fi
    if [[ -z "$ip" ]]; then
      ip="$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for(i=1;i<=NF;i++) if($i=="src"){print $(i+1); exit}}' || true)"
    fi
  fi
  if [[ -z "$ip" ]] && command -v hostname >/dev/null 2>&1; then
    ip="$(hostname -I 2>/dev/null | tr ' ' '\n' | grep -vE '^(127\.|172\.17\.|169\.254\.|$)' | head -n1 || true)"
  fi
  printf '%s' "${ip:-127.0.0.1}"
}

xg_detect_subnet() {
  local ip="$1" iface="${2:-}" cidr=""
  if command -v ip >/dev/null 2>&1; then
    if [[ -n "$iface" ]]; then
      cidr="$(ip -4 -o addr show dev "$iface" 2>/dev/null | awk '{print $4; exit}' || true)"
    fi
    if [[ -z "$cidr" && -n "$ip" ]]; then
      cidr="$(ip -4 -o addr show 2>/dev/null | awk -v ip="$ip" '$4 ~ "^"ip"/" {print $4; exit}' || true)"
    fi
  fi
  if [[ -n "$cidr" ]] && command -v python3 >/dev/null 2>&1; then
    python3 -c "import ipaddress; print(ipaddress.ip_network('$cidr', strict=False))" 2>/dev/null && return 0
  fi
  if [[ "$ip" =~ ^([0-9]+\.[0-9]+\.[0-9]+)\.[0-9]+$ ]]; then
    printf '%s.0/24' "${BASH_REMATCH[1]}"
  else
    printf '192.168.10.0/24'
  fi
}

xg_detect_timezone() {
  local tz=""
  if command -v timedatectl >/dev/null 2>&1; then
    tz="$(timedatectl show -p Timezone --value 2>/dev/null || true)"
  fi
  if [[ -z "$tz" && -f /etc/timezone ]]; then
    tz="$(tr -d ' \r\n' < /etc/timezone || true)"
  fi
  if [[ -z "$tz" && -L /etc/localtime ]]; then
    tz="$(readlink -f /etc/localtime | sed -E 's#^.*/zoneinfo/##' || true)"
  fi
  printf '%s' "${tz:-UTC}"
}

xg_detect_db_timezone() {
  if command -v python3 >/dev/null 2>&1; then
    python3 -c 'from datetime import datetime
off = datetime.now().astimezone().utcoffset()
secs = int(off.total_seconds()) if off else 0
sign = "+" if secs >= 0 else "-"
secs = abs(secs)
print(f"{sign}{secs//3600:02d}:{(secs%3600)//60:02d}")' 2>/dev/null && return 0
  fi
  local z
  z="$(date +%:z 2>/dev/null || true)"
  printf '%s' "${z:-+00:00}"
}

xg_is_port_listening() {
  local port="$1"
  if command -v ss >/dev/null 2>&1; then
    ss -ltnH "sport = :${port}" 2>/dev/null | grep -q .
    return $?
  elif command -v python3 >/dev/null 2>&1; then
    python3 -c "import socket, sys; s=socket.socket(); s.settimeout(0.4); rc=s.connect_ex(('127.0.0.1', int(sys.argv[1]))); s.close(); sys.exit(0 if rc==0 else 1)" "$port"
    return $?
  fi
  return 1
}

# Sets global array XG_COMPOSE_CMD=(docker compose) or (docker-compose). Returns 1 if unavailable.
xg_init_compose_cmd() {
  if command -v docker >/dev/null 2>&1 && docker compose version >/dev/null 2>&1; then
    XG_COMPOSE_CMD=(docker compose)
    return 0
  elif command -v docker-compose >/dev/null 2>&1; then
    XG_COMPOSE_CMD=(docker-compose)
    return 0
  fi
  return 1
}

xg_docker_daemon_ok() {
  command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1
}

xg_find_php() {
  if command -v php >/dev/null 2>&1; then
    if php -r 'exit(version_compare(PHP_VERSION, "8.2.0", ">=") ? 0 : 1);' >/dev/null 2>&1; then
      command -v php
      return 0
    fi
  fi
  return 1
}

xg_find_node() {
  local candidate
  if command -v node >/dev/null 2>&1; then
    local major
    major="$(node -p 'parseInt(process.versions.node.split(".")[0], 10)' 2>/dev/null || echo 0)"
    if [[ "$major" -ge 20 ]]; then
      command -v node
      return 0
    fi
  fi
  for candidate in "$HOME"/.nvm/versions/node/v*/bin/node; do
    [[ -x "$candidate" ]] || continue
    local m
    m="$("$candidate" -p 'parseInt(process.versions.node.split(".")[0], 10)' 2>/dev/null || echo 0)"
    if [[ "$m" -ge 20 ]]; then
      printf '%s' "$candidate"
      return 0
    fi
  done
  return 1
}

xg_find_gateway_bin() {
  local candidates=(
    "$SERVER_DIR/storage/app/bin/xephyguard-screen-gateway"
    "$SCREEN_GW_DIR/xephyguard-screen-gateway"
  )
  local c
  for c in "${candidates[@]}"; do
    if [[ -x "$c" ]]; then
      printf '%s' "$c"
      return 0
    fi
  done
  if command -v xephyguard-screen-gateway >/dev/null 2>&1; then
    command -v xephyguard-screen-gateway
    return 0
  fi
  return 1
}

xg_ca_fingerprint() {
  local crt="$1"
  [[ -f "$crt" ]] || return 1
  openssl x509 -in "$crt" -noout -fingerprint -sha256 2>/dev/null | cut -d= -f2
}

# Detect active / configured runtime mode: docker | prod | native | none
xg_detect_mode() {
  local requested="${1:-auto}"
  case "$requested" in
    docker|prod|native)
      printf '%s' "$requested"
      return 0
      ;;
    auto) ;;
    *) xg_die "Unknown mode '$requested' (expected: auto, docker, prod, native)" ;;
  esac

  # Prefer running stack first
  if xg_docker_daemon_ok; then
    if docker ps --format '{{.Names}}' 2>/dev/null | grep -q '^xephyguard-local-'; then
      printf 'docker'
      return 0
    fi
    if docker ps --format '{{.Names}}' 2>/dev/null | grep -qE '^ccc-deploy-(web|mysql)-|^xephyguard-(web|mysql)$'; then
      printf 'prod'
      return 0
    fi
  fi

  # Otherwise check configured environment files
  if [[ -f "$PID_DIR/laravel.pid" ]] && kill -0 "$(cat "$PID_DIR/laravel.pid" 2>/dev/null || echo 0)" 2>/dev/null; then
    printf 'native'
    return 0
  fi
  if [[ -f "$DEPLOY_DIR/.env.local" ]] && command -v docker >/dev/null 2>&1; then
    printf 'docker'
    return 0
  fi
  if [[ -f "/opt/xephyguard/ccc-deploy/.env" || -f "$DEPLOY_DIR/.env" ]] && command -v docker >/dev/null 2>&1; then
    printf 'prod'
    return 0
  fi
  if [[ -f "$SERVER_DIR/.env" ]]; then
    printf 'native'
    return 0
  fi
  printf 'none'
}

xg_prod_deploy_dir() {
  if [[ -f "/opt/xephyguard/ccc-deploy/.env" ]]; then
    printf '/opt/xephyguard/ccc-deploy'
  elif [[ -f "$DEPLOY_DIR/.env" ]]; then
    printf '%s' "$DEPLOY_DIR"
  else
    printf '%s' "$DEPLOY_DIR"
  fi
}
