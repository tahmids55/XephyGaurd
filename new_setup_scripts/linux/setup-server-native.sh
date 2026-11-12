#!/usr/bin/env bash
# XephyGuard First-Time Server Setup (Linux Native - No Docker required)
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=../../start_server/linux/_common.sh
source "$SCRIPT_DIR/../../start_server/linux/_common.sh"

CCC_IP=""
SUBNET=""
LAN_IF=""
TZ_NAME=""
LAB_NAME="Computer Lab"
APP_PORT=8000
REVERB_PORT=8080
GATEWAY_PORT=8082
WITH_NGINX=0
FORCE_CA=0
SKIP_START=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --ip <CCC_IP>            Server LAN IP (auto-detected if omitted)
  --subnet <CIDR>          Lab CIDR subnet (auto-detected if omitted)
  --tz <IANA_TIMEZONE>     IANA timezone, e.g. Asia/Dhaka, UTC (auto-detected)
  --lab-name <NAME>        Lab display name (default: "Computer Lab")
  --app-port <N>           Laravel HTTP port (default: 8000)
  --reverb-port <N>        Reverb WebSocket port (default: 8080)
  --gateway-port <N>       Screen Gateway port (default: 8082)
  --with-nginx             Install/render Nginx HTTPS (443) + agent mTLS (8443) site (requires sudo)
  --force-ca               Regenerate CA certificate and key
  --skip-start             Finish setup without starting background processes
  -h, --help               Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ip) CCC_IP="${2:?Missing value for --ip}"; shift 2 ;;
    --subnet) SUBNET="${2:?Missing value for --subnet}"; shift 2 ;;
    --tz) TZ_NAME="${2:?Missing value for --tz}"; shift 2 ;;
    --lab-name) LAB_NAME="${2:?Missing value for --lab-name}"; shift 2 ;;
    --app-port) APP_PORT="${2:?Missing value for --app-port}"; shift 2 ;;
    --reverb-port) REVERB_PORT="${2:?Missing value for --reverb-port}"; shift 2 ;;
    --gateway-port) GATEWAY_PORT="${2:?Missing value for --gateway-port}"; shift 2 ;;
    --with-nginx) WITH_NGINX=1; shift ;;
    --force-ca) FORCE_CA=1; shift ;;
    --skip-start) SKIP_START=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) xg_die "Unknown option: $1 (use --help)" ;;
  esac
done

[[ -n "$LAN_IF" ]]  || LAN_IF="$(xg_detect_lan_iface)"
[[ -n "$CCC_IP" ]]  || CCC_IP="$(xg_detect_lan_ip)"
[[ -n "$SUBNET" ]]  || SUBNET="$(xg_detect_subnet "$CCC_IP" "$LAN_IF")"
[[ -n "$TZ_NAME" ]] || TZ_NAME="$(xg_detect_timezone)"

printf "\n==========================================================\n"
printf "        XephyGuard Server Setup (Linux Native)            \n"
printf "==========================================================\n"
printf "Repository : %s\n" "$REPO_ROOT"
printf "LAN IP     : %s\n" "$CCC_IP"
printf "Subnet     : %s\n" "$SUBNET"
printf "Timezone   : %s\n" "$TZ_NAME"
printf "Nginx TLS  : %s\n\n" "$( (( WITH_NGINX == 1 )) && echo 'enabled (80/443/8443)' || echo 'disabled (direct HTTP)' )"

# 1. PHP & extensions check
PHP_BIN="$(xg_find_php || true)"
if [[ -z "$PHP_BIN" ]]; then
  xg_die "PHP 8.2+ is required. On Ubuntu/Debian: sudo apt-get install -y php-cli php-sqlite3 php-mbstring php-xml php-curl php-zip openssl"
fi
xg_ok "PHP found: $("$PHP_BIN" -r 'echo PHP_VERSION;')"

MISSING_EXTS=()
for ext in pdo_sqlite openssl mbstring curl xml; do
  if ! "$PHP_BIN" -m | grep -qiE "^${ext}$"; then
    MISSING_EXTS+=("$ext")
  fi
done
if (( ${#MISSING_EXTS[@]} > 0 )); then
  xg_die "Missing required PHP extensions: ${MISSING_EXTS[*]}"
fi

# 2. Composer dependencies
if [[ ! -f "$SERVER_DIR/vendor/autoload.php" ]]; then
  xg_info "Installing PHP dependencies with Composer..."
  COMPOSER_CMD=()
  if command -v composer >/dev/null 2>&1; then
    COMPOSER_CMD=(composer)
  elif [[ -f "$SERVER_DIR/composer.phar" ]]; then
    COMPOSER_CMD=("$PHP_BIN" "$SERVER_DIR/composer.phar")
  else
    xg_info "Downloading composer.phar locally into server/..."
    "$PHP_BIN" -r "copy('https://getcomposer.org/installer', '$SERVER_DIR/composer-setup.php');"
    "$PHP_BIN" "$SERVER_DIR/composer-setup.php" --install-dir="$SERVER_DIR" --filename=composer.phar
    rm -f "$SERVER_DIR/composer-setup.php"
    COMPOSER_CMD=("$PHP_BIN" "$SERVER_DIR/composer.phar")
  fi
  (cd "$SERVER_DIR" && "${COMPOSER_CMD[@]}" install --no-interaction --prefer-dist)
fi

# 3. Frontend build (Vite requires Node >= 20)
if [[ ! -f "$SERVER_DIR/public/build/manifest.json" ]]; then
  NODE_BIN="$(xg_find_node || true)"
  [[ -n "$NODE_BIN" ]] || xg_die "Node.js >= 20 is required to build assets (Vite). Install Node 20+ or nvm and re-run."
  export PATH="$(dirname "$NODE_BIN"):$PATH"
  xg_info "Building frontend Vite bundle with $(node -v)..."
  (cd "$SERVER_DIR" && npm ci && npm run build)
fi

# 4. Build Screen Gateway binary
GW_BIN="$SERVER_DIR/storage/app/bin/xephyguard-screen-gateway"
mkdir -p "$SERVER_DIR/storage/app/bin"
if [[ ! -x "$GW_BIN" ]]; then
  if command -v go >/dev/null 2>&1; then
    xg_info "Building xephyguard-screen-gateway with local Go..."
    (cd "$SCREEN_GW_DIR" && go build -trimpath -ldflags="-s -w" -o "$GW_BIN" .)
  elif xg_docker_daemon_ok; then
    xg_info "Go not installed locally; building xephyguard-screen-gateway inside ephemeral golang Docker container..."
    docker run --rm \
      -u "$(id -u):$(id -g)" \
      -e GOCACHE=/tmp/gocache \
      -e GOPATH=/tmp/gopath \
      -v "$SCREEN_GW_DIR:/src" \
      -v "$SERVER_DIR/storage/app/bin:/out" \
      -w /src \
      golang:1.23-bookworm \
      go build -trimpath -ldflags="-s -w" -o /out/xephyguard-screen-gateway .
  else
    xg_warn "Neither Go nor Docker is available; skipping screen-gateway binary build."
  fi
fi

# 5. Prepare server/.env
ENV_FILE="$SERVER_DIR/.env"
if [[ ! -f "$ENV_FILE" ]]; then
  cp "$SERVER_DIR/.env.example" "$ENV_FILE"
fi

APP_KEY_NOW="$(xg_read_env "$ENV_FILE" APP_KEY "")"
if [[ -z "$APP_KEY_NOW" ]]; then
  (cd "$SERVER_DIR" && "$PHP_BIN" artisan key:generate --force)
fi

REVERB_KEY_NOW="$(xg_read_env "$ENV_FILE" REVERB_APP_KEY "")"
if [[ -z "$REVERB_KEY_NOW" || "$REVERB_KEY_NOW" == "xg-dev-key" ]]; then
  xg_set_env "$ENV_FILE" REVERB_APP_KEY "$(xg_rand_hex 12)"
fi
REVERB_SEC_NOW="$(xg_read_env "$ENV_FILE" REVERB_APP_SECRET "")"
if [[ -z "$REVERB_SEC_NOW" || "$REVERB_SEC_NOW" == "xg-dev-secret" ]]; then
  xg_set_env "$ENV_FILE" REVERB_APP_SECRET "$(xg_rand_hex 24)"
fi

GW_SEC="$(xg_read_env "$ENV_FILE" SCREEN_GATEWAY_SECRET "")"
[[ -z "$GW_SEC" || "$GW_SEC" == dev-secret* ]] && xg_set_env "$ENV_FILE" SCREEN_GATEWAY_SECRET "$(xg_rand_hex 32)"
GW_ASEC="$(xg_read_env "$ENV_FILE" SCREEN_GATEWAY_AGENT_SECRET "")"
[[ -z "$GW_ASEC" || "$GW_ASEC" == dev-secret* ]] && xg_set_env "$ENV_FILE" SCREEN_GATEWAY_AGENT_SECRET "$(xg_rand_hex 32)"

xg_set_env "$ENV_FILE" APP_NAME "XephyGuard"
xg_set_env "$ENV_FILE" APP_TIMEZONE "$TZ_NAME"
xg_set_env "$ENV_FILE" DB_CONNECTION "sqlite"
xg_set_env "$ENV_FILE" BROADCAST_CONNECTION "reverb"
xg_set_env "$ENV_FILE" QUEUE_CONNECTION "database"
xg_set_env "$ENV_FILE" CACHE_STORE "database"
xg_set_env "$ENV_FILE" SESSION_DRIVER "database"
xg_set_env "$ENV_FILE" APP_PORT "$APP_PORT"
xg_set_env "$ENV_FILE" REVERB_APP_ID "xephyguard"
xg_set_env "$ENV_FILE" REVERB_HOST "127.0.0.1"
xg_set_env "$ENV_FILE" REVERB_PORT "$REVERB_PORT"
xg_set_env "$ENV_FILE" REVERB_SERVER_HOST "0.0.0.0"
xg_set_env "$ENV_FILE" REVERB_SERVER_PORT "$REVERB_PORT"
xg_set_env "$ENV_FILE" SCREEN_GATEWAY_PORT "$GATEWAY_PORT"
xg_set_env "$ENV_FILE" SCREEN_GATEWAY_ISSUER "xephyguard-ccc"
xg_set_env "$ENV_FILE" SCREEN_GATEWAY_AUDIENCE "xephyguard-gateway"
xg_set_env "$ENV_FILE" XG_LAB_NAME "\"${LAB_NAME}\""
xg_set_env "$ENV_FILE" XG_CCC_IP "$CCC_IP"
xg_set_env "$ENV_FILE" XG_LAB_SUBNET "$SUBNET"
xg_set_env "$ENV_FILE" XG_NETWORK_MODE "proxy_only"

if (( WITH_NGINX == 1 )); then
  xg_set_env "$ENV_FILE" APP_URL "https://${CCC_IP}"
  xg_set_env "$ENV_FILE" SESSION_SECURE_COOKIE "true"
  xg_set_env "$ENV_FILE" REVERB_SCHEME "https"
  xg_set_env "$ENV_FILE" SCREEN_GATEWAY_PATH "/screen-gw"
  xg_set_env "$ENV_FILE" SCREEN_GATEWAY_URL ""
  xg_set_env "$ENV_FILE" XG_REQUIRE_MTLS "true"
else
  xg_set_env "$ENV_FILE" APP_URL "http://${CCC_IP}:${APP_PORT}"
  xg_set_env "$ENV_FILE" SESSION_SECURE_COOKIE "false"
  xg_set_env "$ENV_FILE" REVERB_SCHEME "http"
  xg_set_env "$ENV_FILE" SCREEN_GATEWAY_PATH "/"
  xg_set_env "$ENV_FILE" SCREEN_GATEWAY_URL "ws://${CCC_IP}:${GATEWAY_PORT}"
  xg_set_env "$ENV_FILE" XG_REQUIRE_MTLS "false"
fi

# 6. SQLite DB + CA + Bootstrap
mkdir -p "$SERVER_DIR/database" "$SERVER_DIR/storage/app/private/ca" "$SERVER_DIR/storage/app/public/downloads" "$SERVER_DIR/storage/logs"
SQLITE_DB="$SERVER_DIR/database/database.sqlite"
[[ -f "$SQLITE_DB" ]] || touch "$SQLITE_DB"

xg_info "Running SQLite migrations..."
(cd "$SERVER_DIR" && "$PHP_BIN" artisan migrate --force)

CA_CRT="$SERVER_DIR/storage/app/private/ca/ca.crt"
if (( FORCE_CA == 1 )) || [[ ! -f "$CA_CRT" ]]; then
  xg_info "Initializing XephyGuard Lab CA for IP $CCC_IP..."
  FORCE_FLAG=""
  (( FORCE_CA == 1 )) && FORCE_FLAG="--force"
  (cd "$SERVER_DIR" && "$PHP_BIN" artisan xephyguard:ca:init --server-ip="$CCC_IP" --server-dns=localhost --server-dns="$(hostname)" $FORCE_FLAG)
fi

xg_info "Running idempotent bootstrap (lab, profiles, seed rules)..."
(cd "$SERVER_DIR" && "$PHP_BIN" artisan xephyguard:bootstrap)

# 7. Optional Nginx HTTPS/mTLS reverse proxy
if (( WITH_NGINX == 1 )); then
  command -v nginx >/dev/null 2>&1 || xg_die "nginx binary not found. Install nginx first ('sudo apt-get install -y nginx') or omit --with-nginx."
  TMPL="$DEPLOY_DIR/nginx/dev-native.conf.example"
  [[ -f "$TMPL" ]] || xg_die "Missing Nginx template: $TMPL"

  CA_DIR_SLASH="$SERVER_DIR/storage/app/private/ca"
  PUBLIC_SLASH="$SERVER_DIR/public"
  RENDERED_TMP="$(mktemp)"
  sed \
    -e "s|@CA_DIR@|${CA_DIR_SLASH}|g" \
    -e "s|@PUBLIC@|${PUBLIC_SLASH}|g" \
    -e "s|127\.0\.0\.1:8000|127.0.0.1:${APP_PORT}|g" \
    -e "s|127\.0\.0\.1:8080|127.0.0.1:${REVERB_PORT}|g" \
    -e "s|127\.0\.0\.1:8082|127.0.0.1:${GATEWAY_PORT}|g" \
    "$TMPL" > "$RENDERED_TMP"

  TARGET_AVAIL="/etc/nginx/sites-available/xephyguard.conf"
  TARGET_CONF="/etc/nginx/conf.d/xephyguard.conf"
  RUN_AS=()
  (( EUID != 0 )) && RUN_AS=(sudo)

  if [[ -d /etc/nginx/sites-available && -d /etc/nginx/sites-enabled ]]; then
    "${RUN_AS[@]}" cp "$RENDERED_TMP" "$TARGET_AVAIL"
    "${RUN_AS[@]}" chmod 644 "$TARGET_AVAIL"
    "${RUN_AS[@]}" ln -sf "$TARGET_AVAIL" /etc/nginx/sites-enabled/xephyguard.conf
    "${RUN_AS[@]}" rm -f /etc/nginx/sites-enabled/default
  else
    "${RUN_AS[@]}" cp "$RENDERED_TMP" "$TARGET_CONF"
    "${RUN_AS[@]}" chmod 644 "$TARGET_CONF"
  fi
  rm -f "$RENDERED_TMP"

  "${RUN_AS[@]}" nginx -t
  "${RUN_AS[@]}" systemctl enable --now nginx >/dev/null 2>&1 || true
  "${RUN_AS[@]}" systemctl reload nginx 2>/dev/null || "${RUN_AS[@]}" systemctl restart nginx
  xg_ok "Nginx configured on ports 80 / 443 / 8443."
fi

# 8. Start services
if (( SKIP_START == 1 )); then
  xg_ok "Native setup completed (--skip-start specified)."
  echo "Start anytime with: $REPO_ROOT/start_server/linux/start-server.sh --mode native"
else
  "$REPO_ROOT/start_server/linux/start-server.sh" --mode native --app-port "$APP_PORT" --reverb-port "$REVERB_PORT" --gateway-port "$GATEWAY_PORT"
  echo "Next step: Open the Dashboard URL above -> /setup to create your instructor admin account."
fi
