#!/usr/bin/env bash
# XephyGuard Server Status (Linux)
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=_common.sh
source "$SCRIPT_DIR/_common.sh"

MODE="auto"

usage() {
  cat <<EOF
Usage: $(basename "$0") [--mode auto|docker|prod|native] [-h|--help]

Shows live status, active ports, URLs, and CA SHA-256 fingerprint for XephyGuard CCC.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode) MODE="${2:?Missing value for --mode}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) xg_die "Unknown option: $1 (use --help)" ;;
  esac
done

RESOLVED_MODE="$(xg_detect_mode "$MODE")"
LAN_IP="$(xg_detect_lan_ip)"

printf "\n==========================================================\n"
printf "            XephyGuard Server Status (Linux)              \n"
printf "==========================================================\n"
printf "Repository root : %s\n" "$REPO_ROOT"
printf "Detected mode   : %s\n" "$RESOLVED_MODE"
printf "Detected LAN IP : %s\n\n" "$LAN_IP"

print_port_row() {
  local label="$1" port="$2" proto="${3:-TCP}"
  if [[ "$proto" == "UDP" ]]; then
    if command -v ss >/dev/null 2>&1 && ss -lunH "sport = :${port}" 2>/dev/null | grep -q .; then
      printf "  [UP]   %-24s UDP %s\n" "$label" "$port"
    else
      printf "  [DOWN] %-24s UDP %s\n" "$label" "$port"
    fi
    return 0
  fi
  if xg_is_port_listening "$port"; then
    printf "  [UP]   %-24s port %s\n" "$label" "$port"
  else
    printf "  [DOWN] %-24s port %s\n" "$label" "$port"
  fi
}

case "$RESOLVED_MODE" in
  docker)
    xg_init_compose_cmd || xg_die "Docker Compose not available."
    xg_docker_daemon_ok || xg_die "Docker daemon is not running or permission denied."
    ENV_FILE="$DEPLOY_DIR/.env.local"
    COMPOSE_FILE="$DEPLOY_DIR/docker-compose.local.yml"

    "${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" ps
    echo

    APP_URL="$(xg_read_env "$ENV_FILE" APP_URL "https://${LAN_IP}")"
    CCC_IP="$(xg_read_env "$ENV_FILE" XG_CCC_IP "$LAN_IP")"
    GW_PORT="$(xg_read_env "$ENV_FILE" SCREEN_GATEWAY_PORT "8082")"
    REVERB_PORT="$(xg_read_env "$ENV_FILE" REVERB_PORT "8081")"

    echo "Ports:"
    print_port_row "HTTP (CA / Installers)" 80
    print_port_row "HTTPS (Dashboard / WSS)" 443
    print_port_row "mTLS (Agent API)" 8443
    print_port_row "MySQL" 3306
    print_port_row "PHP-FPM / Internal HTTP" 8000
    print_port_row "Reverb WebSocket" "$REVERB_PORT"
    print_port_row "Screen Gateway" "$GW_PORT"
    print_port_row "MITM Proxy" 8080
    print_port_row "Analysis Service" 8090
    print_port_row "Unbound DNS" 53 UDP
    print_port_row "Agent Discovery" 47474 UDP

    CA_FP=""
    if docker exec xephyguard-local-web-1 test -f /var/lib/xephyguard/ca/ca.crt >/dev/null 2>&1; then
      CA_FP="$(docker exec xephyguard-local-web-1 openssl x509 -in /var/lib/xephyguard/ca/ca.crt -noout -fingerprint -sha256 2>/dev/null | cut -d= -f2 || true)"
    elif [[ -f "$SERVER_DIR/storage/app/private/ca/ca.crt" ]]; then
      CA_FP="$(xg_ca_fingerprint "$SERVER_DIR/storage/app/private/ca/ca.crt" || true)"
    fi

    echo
    printf "Dashboard URL     : %s\n" "$APP_URL"
    printf "First-time Setup  : %s/setup\n" "${APP_URL%/}"
    printf "Notice / CA Cert  : http://%s/notice  |  http://%s/ca.crt\n" "$CCC_IP" "$CCC_IP"
    printf "Downloads Page    : http://%s/downloads\n" "$CCC_IP"
    printf "Agent mTLS        : https://%s:8443\n" "$CCC_IP"
    [[ -n "$CA_FP" ]] && printf "CA SHA-256        : %s\n" "$CA_FP"
    ;;

  prod)
    xg_init_compose_cmd || xg_die "Docker Compose not available."
    xg_docker_daemon_ok || xg_die "Docker daemon is not running or permission denied."
    PROD_DIR="$(xg_prod_deploy_dir)"
    ENV_FILE="$PROD_DIR/.env"
    COMPOSE_FILE="$PROD_DIR/docker-compose.yml"

    "${XG_COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" --env-file "$ENV_FILE" ps
    echo

    APP_URL="$(xg_read_env "$ENV_FILE" APP_URL "https://${LAN_IP}")"
    CCC_IP="$(xg_read_env "$ENV_FILE" XG_CCC_IP "$LAN_IP")"
    GW_PORT="$(xg_read_env "$ENV_FILE" SCREEN_GATEWAY_PORT "8082")"

    echo "Ports:"
    print_port_row "HTTP (CA / Installers)" 80
    print_port_row "HTTPS (Dashboard / WSS)" 443
    print_port_row "mTLS (Agent API)" 8443
    print_port_row "MySQL" 3306
    print_port_row "Reverb WebSocket" 8081
    print_port_row "Screen Gateway" "$GW_PORT"
    print_port_row "MITM Proxy" 8080
    print_port_row "Unbound DNS" 53 UDP
    print_port_row "Agent Discovery" 47474 UDP

    CA_FP="$(xg_ca_fingerprint "/var/lib/xephyguard/ca/ca.crt" || true)"
    echo
    printf "Dashboard URL     : %s\n" "$APP_URL"
    printf "First-time Setup  : %s/setup\n" "${APP_URL%/}"
    printf "Notice / CA Cert  : http://%s/notice  |  http://%s/ca.crt\n" "$CCC_IP" "$CCC_IP"
    printf "Agent mTLS        : https://%s:8443\n" "$CCC_IP"
    [[ -n "$CA_FP" ]] && printf "CA SHA-256        : %s\n" "$CA_FP"
    ;;

  native)
    ENV_FILE="$SERVER_DIR/.env"
    APP_PORT="$(xg_read_env "$ENV_FILE" APP_PORT "8000")"
    REVERB_PORT="$(xg_read_env "$ENV_FILE" REVERB_PORT "8080")"
    GW_PORT="$(xg_read_env "$ENV_FILE" SCREEN_GATEWAY_PORT "8082")"
    APP_URL="$(xg_read_env "$ENV_FILE" APP_URL "http://${LAN_IP}:${APP_PORT}")"

    echo "Ports & Workers:"
    print_port_row "Laravel HTTP" "$APP_PORT"
    print_port_row "Reverb WebSocket" "$REVERB_PORT"
    print_port_row "Screen Gateway" "$GW_PORT"
    if xg_is_port_listening 443 || xg_is_port_listening 8443; then
      print_port_row "Nginx HTTP" 80
      print_port_row "Nginx HTTPS" 443
      print_port_row "Nginx mTLS" 8443
    fi

    SCHED_PID=""
    [[ -f "$PID_DIR/scheduler.pid" ]] && SCHED_PID="$(cat "$PID_DIR/scheduler.pid" 2>/dev/null || true)"
    if [[ -n "$SCHED_PID" ]] && kill -0 "$SCHED_PID" 2>/dev/null; then
      printf "  [UP]   %-24s PID %s\n" "Scheduler (schedule:work)" "$SCHED_PID"
    else
      printf "  [DOWN] %-24s\n" "Scheduler (schedule:work)"
    fi

    CA_FP="$(xg_ca_fingerprint "$SERVER_DIR/storage/app/private/ca/ca.crt" || true)"
    echo
    printf "Dashboard URL     : %s\n" "$APP_URL"
    printf "First-time Setup  : %s/setup\n" "${APP_URL%/}"
    printf "Local Direct      : http://127.0.0.1:%s\n" "$APP_PORT"
    if xg_is_port_listening 8443; then
      printf "Agent mTLS (LAN)  : https://%s:8443\n" "$LAN_IP"
    fi
    [[ -n "$CA_FP" ]] && printf "CA SHA-256        : %s\n" "$CA_FP"
    printf "Logs directory    : %s\n" "$LOG_DIR"
    ;;

  none)
    xg_warn "XephyGuard is not configured on this machine yet."
    echo "Run one of the first-time setup scripts first:"
    echo "  Docker mode : $REPO_ROOT/new_setup_scripts/linux/setup-server-docker.sh"
    echo "  Native mode : $REPO_ROOT/new_setup_scripts/linux/setup-server-native.sh"
    exit 1
    ;;
esac
echo
