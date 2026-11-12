#!/usr/bin/env bash
# XephyGuard Server Launcher (Linux)
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=_common.sh
source "$SCRIPT_DIR/_common.sh"

MODE="auto"
RESTART=0
REBUILD=0
SKIP_NGINX=0
APP_PORT=""
REVERB_PORT=""
GATEWAY_PORT=""

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --mode auto|docker|prod|native   Select runtime mode (default: auto-detect)
  --restart                        Stop running services cleanly before starting
  --rebuild                        Rebuild Docker images before starting (docker/prod modes)
  --app-port N                     Override native Laravel port (default: 8000 / .env)
  --reverb-port N                  Override native Reverb port (default: 8080 / .env)
  --gateway-port N                 Override Screen Gateway port (default: 8082 / .env)
  --skip-nginx                     Do not reload/check host Nginx in native mode
  -h, --help                       Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode) MODE="${2:?Missing value for --mode}"; shift 2 ;;
    --restart) RESTART=1; shift ;;
    --rebuild) REBUILD=1; shift ;;
    --app-port) APP_PORT="${2:?Missing value for --app-port}"; shift 2 ;;
    --reverb-port) REVERB_PORT="${2:?Missing value for --reverb-port}"; shift 2 ;;
    --gateway-port) GATEWAY_PORT="${2:?Missing value for --gateway-port}"; shift 2 ;;
    --skip-nginx) SKIP_NGINX=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) xg_die "Unknown option: $1 (use --help)" ;;
  esac
done

RESOLVED_MODE="$(xg_detect_mode "$MODE")"
if [[ "$RESOLVED_MODE" == "none" ]]; then
  xg_die "XephyGuard is not configured yet. Run new_setup_scripts/linux/setup-server-docker.sh or setup-server-native.sh first."
fi

ensure_gateway_secrets() {
  local env_file="$1"
  local s1 s2
  s1="$(xg_read_env "$env_file" SCREEN_GATEWAY_SECRET "")"
  s2="$(xg_read_env "$env_file" SCREEN_GATEWAY_AGENT_SECRET "")"
  if [[ -z "$s1" || "$s1" == dev-secret* ]]; then
    xg_set_env "$env_file" SCREEN_GATEWAY_SECRET "$(xg_rand_hex 32)"
  fi
  if [[ -z "$s2" || "$s2" == dev-secret* ]]; then
    xg_set_env "$env_file" SCREEN_GATEWAY_AGENT_SECRET "$(xg_rand_hex 32)"
  fi
}

if (( RESTART == 1 )); then
  xg_info "Restart requested: stopping running services..."
  "$SCRIPT_DIR/stop-server.sh" --mode "$RESOLVED_MODE" --quiet
  sleep 1
fi

case "$RESOLVED_MODE" in
  docker)
    xg_init_compose_cmd || xg_die "Docker Compose is required."
    xg_docker_daemon_ok || xg_die "Docker daemon is not running or current user lacks Docker socket permission."
    ENV_FILE="$DEPLOY_DIR/.env.local"
    COMPOSE_FILE="$DEPLOY_DIR/docker-compose.local.yml"
    [[ -f "$ENV_FILE" ]] || xg_die "Missing $ENV_FILE. Run $REPO_ROOT/new_setup_scripts/linux/setup-server-docker.sh first."

    ensure_gateway_secrets "$ENV_FILE"

    xg_info "Starting XephyGuard local Docker stack..."
    UP_ARGS=(-f "$COMPOSE_FILE" --env-file "$ENV_FILE" up -d)
    if (( REBUILD == 1 )); then
      UP_ARGS+=(--build)
    fi
    "${XG_COMPOSE_CMD[@]}" "${UP_ARGS[@]}"

    xg_info "Waiting for web & mysql health..."
    for _ in {1..30}; do
      if xg_is_port_listening 80 || xg_is_port_listening 443; then
        break
      fi
      sleep 1
    done
    ;;

  prod)
    xg_init_compose_cmd || xg_die "Docker Compose is required."
    xg_docker_daemon_ok || xg_die "Docker daemon is not running or requires sudo."
    PROD_DIR="$(xg_prod_deploy_dir)"
    ENV_FILE="$PROD_DIR/.env"
    COMPOSE_FILE="$PROD_DIR/docker-compose.yml"
    [[ -f "$ENV_FILE" ]] || xg_die "Missing $ENV_FILE. Run new_setup_scripts/linux/setup-server-docker.sh --mode gateway|proxy_only first."

    ensure_gateway_secrets "$ENV_FILE"

    xg_info "Starting XephyGuard production Docker stack ($PROD_DIR)..."
    UP_ARGS=(-f "$COMPOSE_FILE" --env-file "$ENV_FILE" up -d)
    if (( REBUILD == 1 )); then
      UP_ARGS+=(--build)
    fi
    "${XG_COMPOSE_CMD[@]}" "${UP_ARGS[@]}"
    ;;

  native)
    ENV_FILE="$SERVER_DIR/.env"
    [[ -f "$ENV_FILE" ]] || xg_die "Missing $ENV_FILE. Run $REPO_ROOT/new_setup_scripts/linux/setup-server-native.sh first."
    PHP_BIN="$(xg_find_php || true)"
    [[ -n "$PHP_BIN" ]] || xg_die "PHP 8.2+ not found on PATH."

    ensure_gateway_secrets "$ENV_FILE"

    [[ -n "$APP_PORT" ]]     || APP_PORT="$(xg_read_env "$ENV_FILE" APP_PORT "8000")"
    [[ -n "$REVERB_PORT" ]]  || REVERB_PORT="$(xg_read_env "$ENV_FILE" REVERB_PORT "8080")"
    [[ -n "$GATEWAY_PORT" ]] || GATEWAY_PORT="$(xg_read_env "$ENV_FILE" SCREEN_GATEWAY_PORT "8082")"

    GW_SECRET="$(xg_read_env "$ENV_FILE" SCREEN_GATEWAY_SECRET)"
    GW_AGENT_SECRET="$(xg_read_env "$ENV_FILE" SCREEN_GATEWAY_AGENT_SECRET)"
    GW_ISSUER="$(xg_read_env "$ENV_FILE" SCREEN_GATEWAY_ISSUER "xephyguard-ccc")"
    GW_AUDIENCE="$(xg_read_env "$ENV_FILE" SCREEN_GATEWAY_AUDIENCE "xephyguard-gateway")"

    mkdir -p "$PID_DIR" "$LOG_DIR"

    NGINX_FRONT=0
    if (( SKIP_NGINX == 0 )) && [[ -f /etc/nginx/sites-enabled/xephyguard.conf || -f /etc/nginx/conf.d/xephyguard.conf ]]; then
      if command -v nginx >/dev/null 2>&1; then
        NGINX_FRONT=1
        if [[ $EUID -eq 0 ]]; then
          nginx -t && systemctl reload nginx 2>/dev/null || systemctl restart nginx 2>/dev/null || true
        elif sudo -n true 2>/dev/null; then
          sudo nginx -t && sudo systemctl reload nginx 2>/dev/null || sudo systemctl restart nginx 2>/dev/null || true
        else
          xg_warn "Host Nginx config exists; reload Nginx with sudo if config changed."
        fi
      fi
    fi

    BIND_HOST="0.0.0.0"
    if (( NGINX_FRONT == 1 )) && xg_is_port_listening 443; then
      BIND_HOST="127.0.0.1"
    fi

    spawn_native() {
      local name="$1" port="$2" log_file="$3"
      shift 3
      local pidfile="$PID_DIR/${name}.pid"
      if [[ "$port" != "0" ]] && xg_is_port_listening "$port"; then
        xg_ok "$name already listening on port $port"
        return 0
      fi
      if [[ -f "$pidfile" ]]; then
        local existing
        existing="$(tr -d ' \r\n' < "$pidfile" || true)"
        if [[ -n "$existing" ]] && kill -0 "$existing" 2>/dev/null; then
          xg_ok "$name already running (PID $existing)"
          return 0
        fi
      fi
      xg_info "Starting $name..."
      nohup "$@" >"$log_file" 2>&1 &
      echo $! > "$pidfile"
    }

    (
      cd "$SERVER_DIR"
      spawn_native "laravel" "$APP_PORT" "$LOG_DIR/native-laravel.log" \
        "$PHP_BIN" artisan serve --host="$BIND_HOST" --port="$APP_PORT"

      spawn_native "reverb" "$REVERB_PORT" "$LOG_DIR/native-reverb.log" \
        "$PHP_BIN" artisan reverb:start --host="$BIND_HOST" --port="$REVERB_PORT"

      spawn_native "scheduler" 0 "$LOG_DIR/native-scheduler.log" \
        "$PHP_BIN" artisan schedule:work
    )

    GW_BIN="$(xg_find_gateway_bin || true)"
    if [[ -z "$GW_BIN" ]] && command -v go >/dev/null 2>&1 && [[ -d "$SCREEN_GW_DIR" ]]; then
      xg_info "Building screen-gateway binary with Go..."
      mkdir -p "$SERVER_DIR/storage/app/bin"
      (cd "$SCREEN_GW_DIR" && go build -trimpath -ldflags="-s -w" -o "$SERVER_DIR/storage/app/bin/xephyguard-screen-gateway" .)
      GW_BIN="$SERVER_DIR/storage/app/bin/xephyguard-screen-gateway"
    fi

    if [[ -n "$GW_BIN" ]]; then
      spawn_native "screen-gateway" "$GATEWAY_PORT" "$LOG_DIR/native-gateway.log" \
        env \
        SCREEN_GATEWAY_ADDR=":${GATEWAY_PORT}" \
        SCREEN_GATEWAY_SECRET="$GW_SECRET" \
        SCREEN_GATEWAY_AGENT_SECRET="$GW_AGENT_SECRET" \
        SCREEN_GATEWAY_ISSUER="$GW_ISSUER" \
        SCREEN_GATEWAY_AUDIENCE="$GW_AUDIENCE" \
        "$GW_BIN"
    else
      xg_warn "Screen Gateway binary not found and 'go' CLI unavailable; live screen viewer relay will stay down until built."
    fi

    sleep 2
    ;;
esac

"$SCRIPT_DIR/status-server.sh" --mode "$RESOLVED_MODE"
