#!/usr/bin/env bash
# XephyGuard Server Stopper (Linux)
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=_common.sh
source "$SCRIPT_DIR/_common.sh"

MODE="auto"
QUIET=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [--mode auto|docker|prod|native] [--quiet] [-h|--help]

Cleanly stops running XephyGuard CCC services while preserving database and CA volumes.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode) MODE="${2:?Missing value for --mode}"; shift 2 ;;
    --quiet) QUIET=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) xg_die "Unknown option: $1 (use --help)" ;;
  esac
done

log() { (( QUIET == 0 )) && xg_info "$*" || true; }

RESOLVED_MODE="$(xg_detect_mode "$MODE")"

stop_pid_file() {
  local name="$1"
  local pidfile="$PID_DIR/${name}.pid"
  if [[ -f "$pidfile" ]]; then
    local pid
    pid="$(tr -d ' \r\n' < "$pidfile" || true)"
    if [[ -n "$pid" && "$pid" =~ ^[0-9]+$ ]] && kill -0 "$pid" 2>/dev/null; then
      log "Stopping $name (PID $pid)..."
      kill "$pid" 2>/dev/null || true
      for _ in {1..15}; do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.2
      done
      if kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid" 2>/dev/null || true
      fi
    fi
    rm -f "$pidfile"
  fi
}

case "$RESOLVED_MODE" in
  docker)
    xg_init_compose_cmd || xg_die "Docker Compose not available."
    xg_docker_daemon_ok || xg_die "Docker daemon is not running or permission denied."
    log "Stopping XephyGuard local Docker stack..."
    "${XG_COMPOSE_CMD[@]}" -f "$DEPLOY_DIR/docker-compose.local.yml" --env-file "$DEPLOY_DIR/.env.local" stop
    ;;

  prod)
    xg_init_compose_cmd || xg_die "Docker Compose not available."
    xg_docker_daemon_ok || xg_die "Docker daemon is not running or permission denied."
    PROD_DIR="$(xg_prod_deploy_dir)"
    log "Stopping XephyGuard production Docker stack ($PROD_DIR)..."
    "${XG_COMPOSE_CMD[@]}" -f "$PROD_DIR/docker-compose.yml" --env-file "$PROD_DIR/.env" stop
    ;;

  native)
    mkdir -p "$PID_DIR"
    stop_pid_file "scheduler"
    stop_pid_file "screen-gateway"
    stop_pid_file "reverb"
    stop_pid_file "laravel"

    # Also clean up any untracked workspace native PHP / gateway processes
    if command -v pkill >/dev/null 2>&1; then
      pkill -f "artisan (serve|reverb:start|schedule:work)" 2>/dev/null || true
      pkill -f "xephyguard-screen-gateway" 2>/dev/null || true
    fi
    ;;

  none)
    (( QUIET == 0 )) && xg_warn "No configured XephyGuard installation detected."
    ;;
esac

(( QUIET == 0 )) && xg_ok "XephyGuard server stopped." || true
