#!/bin/sh
set -eu

MODE="${XG_NETWORK_MODE:-}"
STATE_FILE="/var/lib/xephyguard/net/state.json"

if [ -z "$MODE" ] && [ -f "$STATE_FILE" ]; then
  MODE=$(python3 -c 'import json,sys; print((json.load(open(sys.argv[1])).get("network_mode") or "").strip())' "$STATE_FILE" 2>/dev/null || true)
fi

if [ "$MODE" = "proxy_only" ]; then
  MODE_ARGS="--mode regular@0.0.0.0:8080 --mode transparent@0.0.0.0:8085"
else
  MODE_ARGS="--mode transparent@0.0.0.0:8080 --mode regular@0.0.0.0:8085"
fi

# shellcheck disable=SC2086
exec mitmdump \
  $MODE_ARGS \
  --showhost \
  --set block_global=false \
  --set connection_strategy=lazy \
  --set termlog_verbosity=warn \
  -s /opt/xephyguard/proxy/addons/xephyguard_addon.py \
  --set xg_policy=/var/lib/xephyguard/net/proxy.json
