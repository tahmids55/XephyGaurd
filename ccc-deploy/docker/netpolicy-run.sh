#!/bin/sh
# Starts Unbound and keeps DNS + nftables in sync with /var/lib/xephyguard/net/state.json.
set -e
OUT=/var/lib/xephyguard/net
mkdir -p "$OUT"
[ -f "$OUT/unbound-xephyguard.conf" ] || echo "# waiting for first policy state" > "$OUT/unbound-xephyguard.conf"
cd /opt/xephyguard/proxy
python3 -m xg_netpolicy.generate --state "$OUT/state.json" --out "$OUT" || true
UNBOUND_CONF=/etc/unbound/unbound.conf
if [ -f /etc/unbound/unbound-local.conf.tmpl ]; then
  CCC_IP="${XG_CCC_IP:-}"
  if [ -z "$CCC_IP" ] && [ -f "$OUT/state.json" ]; then
    CCC_IP=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d.get("ccc_ip") or "")' "$OUT/state.json" 2>/dev/null || true)
  fi
  IF_LINES="    interface: 127.0.0.1"
  if [ -n "$CCC_IP" ] && [ "$CCC_IP" != "127.0.0.1" ]; then
    IF_LINES="    interface: ${CCC_IP}\n${IF_LINES}"
  fi
  python3 -c 'import sys; t=open(sys.argv[1]).read(); open(sys.argv[2],"w").write(t.replace("__INTERFACE_LINES__", sys.argv[3].replace("\\n","\n")))' \
    /etc/unbound/unbound-local.conf.tmpl "$OUT/unbound.conf" "$IF_LINES"
  UNBOUND_CONF="$OUT/unbound.conf"
fi
unbound-checkconf "$UNBOUND_CONF"
unbound -d -c "$UNBOUND_CONF" &
UNBOUND=$!
trap 'kill $UNBOUND' TERM INT
python3 -m xg_netpolicy.generate --state "$OUT/state.json" --out "$OUT" --watch --apply &
wait $UNBOUND
