#!/usr/bin/env bash
# XephyGuard First-Time Server Setup (Linux Docker)
# Supports:
#   --mode local       : Self-contained workstation Docker Compose stack (docker-compose.local.yml)
#   --mode proxy_only  : Dedicated Ubuntu Server CCC (DNS/Proxy/Dashboard/mTLS, invokes ccc-deploy/install.sh)
#   --mode gateway     : Dedicated Ubuntu Server CCC Gateway Router + nftables (invokes ccc-deploy/install.sh)
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=../../start_server/linux/_common.sh
source "$SCRIPT_DIR/../../start_server/linux/_common.sh"

MODE="local"
CCC_IP=""
SUBNET=""
LAN_IF=""
WAN_IF=""
TZ_NAME=""
LAB_NAME="Computer Lab"
RESEED=0
REBUILD=0
FORCE_CA=0
SKIP_START=0
YES=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --mode local|proxy_only|gateway  Deployment mode (default: local)
  --ip <CCC_IP>                    Server LAN IP (auto-detected if omitted)
  --subnet <CIDR>                  Lab CIDR subnet (auto-detected if omitted)
  --lan-if <IFACE>                 LAN network interface (auto-detected if omitted)
  --wan-if <IFACE>                 WAN network interface (gateway/proxy_only; auto-detected)
  --tz <IANA_TIMEZONE>             IANA timezone, e.g. Asia/Dhaka, UTC (auto-detected)
  --lab-name <NAME>                Lab display name (default: "Computer Lab")
  --reseed                         Re-import host database.sqlite into MySQL (local mode)
  --rebuild                        Force rebuild of Docker images (local mode)
  --force-ca                       Regenerate CA inside Docker even if host ca.crt exists
  --skip-start                     Complete initialization without keeping stack started
  --yes, -y                        Non-interactive mode for production install.sh
  -h, --help                       Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode) MODE="${2:?Missing value for --mode}"; shift 2 ;;
    --ip) CCC_IP="${2:?Missing value for --ip}"; shift 2 ;;
    --subnet) SUBNET="${2:?Missing value for --subnet}"; shift 2 ;;
    --lan-if) LAN_IF="${2:?Missing value for --lan-if}"; shift 2 ;;
    --wan-if) WAN_IF="${2:?Missing value for --wan-if}"; shift 2 ;;
    --tz) TZ_NAME="${2:?Missing value for --tz}"; shift 2 ;;
    --lab-name) LAB_NAME="${2:?Missing value for --lab-name}"; shift 2 ;;
    --reseed) RESEED=1; shift ;;
    --rebuild) REBUILD=1; shift ;;
    --force-ca) FORCE_CA=1; shift ;;
    --skip-start) SKIP_START=1; shift ;;
    --yes|-y) YES=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) xg_die "Unknown option: $1 (use --help)" ;;
  esac
done

case "$MODE" in
  local|proxy_only|gateway) ;;
  *) xg_die "Invalid --mode '$MODE'. Expected: local, proxy_only, or gateway." ;;
esac

IP_EXPLICIT=0
[[ -n "$CCC_IP" ]] && IP_EXPLICIT=1
SUBNET_EXPLICIT=0
[[ -n "$SUBNET" ]] && SUBNET_EXPLICIT=1
TZ_EXPLICIT=0
[[ -n "$TZ_NAME" ]] && TZ_EXPLICIT=1

[[ -n "$LAN_IF" ]]  || LAN_IF="$(xg_detect_lan_iface)"
[[ -n "$WAN_IF" ]]  || WAN_IF="$LAN_IF"
[[ -n "$CCC_IP" ]]  || CCC_IP="$(xg_detect_lan_ip)"
[[ -n "$SUBNET" ]]  || SUBNET="$(xg_detect_subnet "$CCC_IP" "$LAN_IF")"
[[ -n "$TZ_NAME" ]] || TZ_NAME="$(xg_detect_timezone)"

compute_tz_offset() {
  local tz="$1"
  if command -v python3 >/dev/null 2>&1; then
    python3 - "$tz" <<'PY' 2>/dev/null && return 0
import sys
from datetime import datetime
try:
    from zoneinfo import ZoneInfo
    off = datetime.now(ZoneInfo(sys.argv[1])).utcoffset()
except Exception:
    off = datetime.now().astimezone().utcoffset()
secs = int(off.total_seconds()) if off else 0
sign = "+" if secs >= 0 else "-"
secs = abs(secs)
print(f"{sign}{secs // 3600:02d}:{(secs % 3600) // 60:02d}")
PY
  fi
  xg_detect_db_timezone
}

DB_TZ="$(compute_tz_offset "$TZ_NAME")"

printf "\n==========================================================\n"
printf "        XephyGuard Server Setup (Linux Docker)            \n"
printf "==========================================================\n"
printf "Mode      : %s\n" "$MODE"
printf "LAN IP    : %s (%s)\n" "$CCC_IP" "$LAN_IF"
printf "Subnet    : %s\n" "$SUBNET"
printf "Timezone  : %s (%s)\n" "$TZ_NAME" "$DB_TZ"
printf "Lab Name  : %s\n\n" "$LAB_NAME"

# Production Linux server deployment delegates to ccc-deploy/install.sh cleanly.
if [[ "$MODE" == "gateway" || "$MODE" == "proxy_only" ]]; then
  INSTALL_SH="$DEPLOY_DIR/install.sh"
  [[ -f "$INSTALL_SH" ]] || xg_die "Missing $INSTALL_SH"
  ARGS=(--mode "$MODE" --ccc-ip "$CCC_IP" --subnet "$SUBNET" --lan-if "$LAN_IF" --wan-if "$WAN_IF" --lab-name "$LAB_NAME")
  (( YES == 1 )) && ARGS+=(--yes)
  xg_info "Delegating production host setup to ccc-deploy/install.sh..."
  if [[ $EUID -eq 0 ]]; then
    bash "$INSTALL_SH" "${ARGS[@]}"
  else
    sudo bash "$INSTALL_SH" "${ARGS[@]}"
  fi
  xg_ok "Production installation finished."
  if (( SKIP_START == 0 )); then
    "$REPO_ROOT/start_server/linux/status-server.sh" --mode prod || true
  fi
  exit 0
fi

# ---- Local workstation Docker Compose mode ----
xg_init_compose_cmd || xg_die "Docker Compose v2 ('docker compose') or 'docker-compose' is required. Install Docker first."
if ! xg_docker_daemon_ok; then
  xg_die "Cannot connect to Docker daemon. Run 'sudo systemctl enable --now docker' and ensure '$USER' is in the 'docker' group ('sudo usermod -aG docker $USER')."
fi

ENV_FILE="$DEPLOY_DIR/.env.local"
COMPOSE_FILE="$DEPLOY_DIR/docker-compose.local.yml"

if [[ ! -f "$ENV_FILE" ]]; then
  xg_info "Generating fresh $ENV_FILE..."
  APP_KEY_B64="$(openssl rand -base64 32 2>/dev/null || python3 -c 'import os,base64; print(base64.b64encode(os.urandom(32)).decode())')"
  cat > "$ENV_FILE" <<EOF
APP_NAME=XephyGuard
APP_ENV=production
APP_KEY=base64:${APP_KEY_B64}
APP_DEBUG=false
APP_URL=https://${CCC_IP}
APP_TIMEZONE=${TZ_NAME}
DB_TIMEZONE=${DB_TZ}
TZ=${TZ_NAME}
LOG_CHANNEL=daily
DB_CONNECTION=mysql
DB_HOST=127.0.0.1
DB_PORT=3306
DB_DATABASE=xephyguard
DB_USERNAME=xephyguard
DB_PASSWORD=$(xg_rand_hex 16)
DB_ROOT_PASSWORD=$(xg_rand_hex 16)
SESSION_DRIVER=database
SESSION_SECURE_COOKIE=true
QUEUE_CONNECTION=database
CACHE_STORE=database
BROADCAST_CONNECTION=reverb
REVERB_APP_ID=xephyguard
REVERB_APP_KEY=$(xg_rand_hex 10)
REVERB_APP_SECRET=$(xg_rand_hex 24)
REVERB_HOST=127.0.0.1
REVERB_PORT=8081
REVERB_SCHEME=http
XG_LAB_NAME="${LAB_NAME}"
XG_CCC_IP=${CCC_IP}
XG_LAB_SUBNET=${SUBNET}
XG_LAN_IF=${LAN_IF}
XG_WAN_IF=${WAN_IF}
XG_NETWORK_MODE=proxy_only
XG_CA_DIR=/var/lib/xephyguard/ca
XG_NET_STATE=/var/lib/xephyguard/net/state.json
XG_INTERNAL_TOKEN=$(xg_rand_hex 24)
XG_INTERNAL_URL=http://127.0.0.1:8000
XG_ANALYSIS_URL=http://127.0.0.1:8090
XG_REQUIRE_MTLS=true
SCREEN_GATEWAY_ADDR=:8082
SCREEN_GATEWAY_PORT=8082
SCREEN_GATEWAY_PATH=/screen-gw
SCREEN_GATEWAY_ISSUER=xephyguard-ccc
SCREEN_GATEWAY_AUDIENCE=xephyguard-gateway
SCREEN_GATEWAY_SECRET=$(xg_rand_hex 32)
SCREEN_GATEWAY_AGENT_SECRET=$(xg_rand_hex 32)
SCREEN_GATEWAY_URL=
EOF
else
  xg_info "Updating existing $ENV_FILE cleanly (keeping existing keys/secrets)..."
  OLD_IP="$(xg_read_env "$ENV_FILE" XG_CCC_IP "")"
  IP_ON_HOST=0
  if [[ -n "$OLD_IP" ]] && command -v ip >/dev/null 2>&1; then
    ip -4 -o addr show 2>/dev/null | grep -qF " ${OLD_IP}/" && IP_ON_HOST=1 || true
  fi
  if (( IP_EXPLICIT == 1 )) || [[ -z "$OLD_IP" ]] || (( IP_ON_HOST == 0 )); then
    xg_set_env "$ENV_FILE" XG_CCC_IP "$CCC_IP"
    xg_set_env "$ENV_FILE" APP_URL "https://${CCC_IP}"
    xg_set_env "$ENV_FILE" XG_LAN_IF "$LAN_IF"
    xg_set_env "$ENV_FILE" XG_WAN_IF "$WAN_IF"
    (( IP_ON_HOST == 0 && IP_EXPLICIT == 0 )) && xg_warn "Previous IP '$OLD_IP' not on this host; updated to '$CCC_IP'."
  fi
  if (( SUBNET_EXPLICIT == 1 )) || (( IP_ON_HOST == 0 )); then
    xg_set_env "$ENV_FILE" XG_LAB_SUBNET "$SUBNET"
  fi
  if (( TZ_EXPLICIT == 1 )) || [[ -z "$(xg_read_env "$ENV_FILE" APP_TIMEZONE "")" ]]; then
    xg_set_env "$ENV_FILE" APP_TIMEZONE "$TZ_NAME"
    xg_set_env "$ENV_FILE" DB_TIMEZONE "$DB_TZ"
    xg_set_env "$ENV_FILE" TZ "$TZ_NAME"
  fi
  [[ -z "$(xg_read_env "$ENV_FILE" SCREEN_GATEWAY_SECRET "")" ]] && xg_set_env "$ENV_FILE" SCREEN_GATEWAY_SECRET "$(xg_rand_hex 32)"
  [[ -z "$(xg_read_env "$ENV_FILE" SCREEN_GATEWAY_AGENT_SECRET "")" ]] && xg_set_env "$ENV_FILE" SCREEN_GATEWAY_AGENT_SECRET "$(xg_rand_hex 32)"
fi

if (( REBUILD == 1 )) || ! docker image inspect xephyguard/ccc-app:local >/dev/null 2>&1 || ! docker image inspect xephyguard/ccc-screen-gateway:local >/dev/null 2>&1; then
  xg_info "Building Docker images (first run may take several minutes)..."
  "${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" build
fi

xg_info "Starting MySQL container..."
"${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" up -d mysql

DB_ROOT_PW="$(xg_read_env "$ENV_FILE" DB_ROOT_PASSWORD)"
DB_PASS="$(xg_read_env "$ENV_FILE" DB_PASSWORD)"

for _ in {1..45}; do
  if "${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" exec -T mysql \
      mysqladmin ping -h 127.0.0.1 -uroot "-p${DB_ROOT_PW}" >/dev/null 2>&1; then
    break
  fi
  sleep 2
done
"${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" exec -T mysql \
  mysqladmin ping -h 127.0.0.1 -uroot "-p${DB_ROOT_PW}" >/dev/null 2>&1 || xg_die "MySQL failed to become healthy."

# ---- CA & mitmproxy initialization / seeding ----
HOST_CA_DIR="$SERVER_DIR/storage/app/private/ca"
if (( FORCE_CA == 0 )) && [[ -f "$HOST_CA_DIR/ca.crt" && -f "$HOST_CA_DIR/ca.key" ]]; then
  xg_info "Seeding CA from host $HOST_CA_DIR..."
  tar -C "$HOST_CA_DIR" -cf - . | \
    "${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" run --rm --no-deps -T --entrypoint sh web -c \
    'mkdir -p /var/lib/xephyguard/ca && tar -C /var/lib/xephyguard/ca -xf - && chmod 711 /var/lib/xephyguard/ca && chmod 600 /var/lib/xephyguard/ca/*.key 2>/dev/null || true'
else
  HAS_VOL_CA=0
  "${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" run --rm --no-deps -T --entrypoint test web -f /var/lib/xephyguard/ca/ca.crt && HAS_VOL_CA=1 || true
  if (( FORCE_CA == 1 )) || (( HAS_VOL_CA == 0 )); then
    xg_info "Initializing XephyGuard Lab CA inside container for IP $CCC_IP..."
    FORCE_FLAG=""
    (( FORCE_CA == 1 )) && FORCE_FLAG="--force"
    "${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" run --rm --no-deps --entrypoint php web \
      /var/www/html/artisan xephyguard:ca:init --server-ip="$CCC_IP" --server-dns=localhost --server-dns="$(hostname)" $FORCE_FLAG
  else
    xg_ok "Existing CA found inside xg_local_ca volume."
  fi
fi

# Sync mitmproxy CA bundle from xg_local_ca -> xg_local_mitmproxy (uid 1500)
TMP_CA_PEM="$(mktemp)"
trap 'rm -f "$TMP_CA_PEM"' EXIT
"${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" run --rm --no-deps -T --entrypoint sh web -c \
  'cat /var/lib/xephyguard/ca/ca.key /var/lib/xephyguard/ca/ca.crt' > "$TMP_CA_PEM"

"${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" run --rm --no-deps -T -u root --entrypoint sh proxy -c \
  'mkdir -p /home/mitmproxy/.mitmproxy && cat > /home/mitmproxy/.mitmproxy/mitmproxy-ca.pem && openssl x509 -in /home/mitmproxy/.mitmproxy/mitmproxy-ca.pem -out /home/mitmproxy/.mitmproxy/mitmproxy-ca-cert.pem 2>/dev/null || cp /home/mitmproxy/.mitmproxy/mitmproxy-ca.pem /home/mitmproxy/.mitmproxy/mitmproxy-ca-cert.pem; chown -R 1500:1500 /home/mitmproxy/.mitmproxy && chmod 600 /home/mitmproxy/.mitmproxy/mitmproxy-ca.pem' < "$TMP_CA_PEM"

# ---- Seed agent downloads if present on host ----
if [[ -d "$SERVER_DIR/storage/app/public/downloads" ]] && [[ -n "$(ls -A "$SERVER_DIR/storage/app/public/downloads" 2>/dev/null)" ]]; then
  xg_info "Copying agent installer binaries into storage volume..."
  tar -C "$SERVER_DIR/storage/app/public/downloads" -cf - . | \
    "${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" run --rm --no-deps -T --entrypoint sh web -c \
    'mkdir -p /var/www/html/storage/app/public/downloads && tar -C /var/www/html/storage/app/public/downloads -xf -'
fi

# ---- Migrations, SQLite import OR fresh bootstrap ----
xg_info "Running database migrations..."
"${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" run --rm --no-deps --entrypoint php web \
  /var/www/html/artisan migrate --force

USER_COUNT="$("${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" exec -T mysql \
  mysql -uxephyguard "-p${DB_PASS}" xephyguard -Nse "SELECT COUNT(*) FROM users" 2>/dev/null | tr -d ' \r\n' || echo 0)"
LAB_COUNT="$("${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" exec -T mysql \
  mysql -uxephyguard "-p${DB_PASS}" xephyguard -Nse "SELECT COUNT(*) FROM labs" 2>/dev/null | tr -d ' \r\n' || echo 0)"

SQLITE_DB="$SERVER_DIR/database/database.sqlite"
if [[ -f "$SQLITE_DB" ]] && { [[ "${USER_COUNT:-0}" -eq 0 ]] || (( RESEED == 1 )); }; then
  xg_info "Importing existing SQLite database into MySQL..."
  TMP_TAR_DIR="$(mktemp -d)"
  cp "$SQLITE_DB" "$TMP_TAR_DIR/import.sqlite"
  cp "$DEPLOY_DIR/import-sqlite-to-local.php" "$TMP_TAR_DIR/import.php"
  tar -C "$TMP_TAR_DIR" -cf - import.sqlite import.php | \
    "${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" run --rm --no-deps -T --entrypoint sh web -c \
    'tar -C /tmp -xf - && php /tmp/import.php /tmp/import.sqlite && rm -f /tmp/import.sqlite /tmp/import.php'
  rm -rf "$TMP_TAR_DIR"
elif [[ "${LAB_COUNT:-0}" -eq 0 ]]; then
  xg_info "Fresh install detected: seeding lab, built-in profiles, blocklists & rules..."
  "${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" run --rm --no-deps --entrypoint php web \
    /var/www/html/artisan xephyguard:bootstrap
fi

xg_info "Updating network state & file permissions..."
"${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" run --rm --no-deps --entrypoint sh web -c \
  'mkdir -p /var/lib/xephyguard/net /var/www/html/storage/app/public/downloads && php /var/www/html/artisan xephyguard:net:write && chown -R 33:33 /var/lib/xephyguard/net /var/www/html/storage /var/www/html/storage/logs'

if command -v ss >/dev/null 2>&1 && ss -lunH "sport = :53" 2>/dev/null | grep -qE "0\.0\.0\.0:53|${CCC_IP}:53"; then
  xg_warn "Another DNS service is already listening on port 53 (${CCC_IP}); unbound container may conflict unless disabled."
fi

if (( SKIP_START == 1 )); then
  "${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" stop mysql
  xg_ok "Docker environment initialized (not started due to --skip-start)."
else
  "$REPO_ROOT/start_server/linux/start-server.sh" --mode docker
  echo "Next steps:"
  echo "  1. Open the Dashboard URL above -> visit /setup to create the instructor admin account."
  echo "  2. Run 'new_setup_scripts/linux/install-agent.sh --ccc $CCC_IP --key <ENROLL_KEY>' on Linux client PCs,"
  echo "     or 'new_setup_scripts\\windows\\deploy-agents.ps1' from Windows."
fi
