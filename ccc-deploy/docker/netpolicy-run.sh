#!/bin/sh
# Starts Unbound and keeps DNS + nftables in sync with /var/lib/xephyguard/net/state.json.
set -e
OUT=/var/lib/xephyguard/net
mkdir -p "$OUT"
[ -f "$OUT/unbound-xephyguard.conf" ] || echo "# waiting for first policy state" > "$OUT/unbound-xephyguard.conf"
cd /opt/xephyguard/proxy
python3 -m xg_netpolicy.generate --state "$OUT/state.json" --out "$OUT" || true
unbound-checkconf /etc/unbound/unbound.conf
unbound -d -c /etc/unbound/unbound.conf &
UNBOUND=$!
trap 'kill $UNBOUND' TERM INT
python3 -m xg_netpolicy.generate --state "$OUT/state.json" --out "$OUT" --watch --apply &
wait $UNBOUND
