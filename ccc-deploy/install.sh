#!/usr/bin/env bash
# XephyGuard CCC installer (Ubuntu Server 22.04 / 24.04 LTS).
#
#   sudo ./ccc-deploy/install.sh                      # interactive
#   sudo ./ccc-deploy/install.sh --lan-if enp2s0 --wan-if enp1s0 --ccc-ip 192.168.10.2 \
#        --subnet 192.168.10.0/24 --lab-name "Lab 3" --mode gateway --yes
#
# Installs Docker, writes secrets, creates the internal CA, renders nftables rules, starts the
# Compose stack and prints the dashboard URL. Safe to re-run (keeps existing secrets, CA and data).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEPLOY="$ROOT/ccc-deploy"
STATE=/var/lib/xephyguard
LAN_IF="" WAN_IF="" CCC_IP="" SUBNET="" LAB_NAME="Computer Lab" MODE="gateway" YES=0 TZ_NAME=""

say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m  %s\n' "$*"; }
die()  { printf '\033[1;31mxx\033[0m  %s\n' "$*" >&2; exit 1; }
ask()  { local v; [ "$YES" = 1 ] && { echo "$2"; return; }; read -r -p "$1 [$2]: " v; echo "${v:-$2}"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --lan-if) LAN_IF=$2; shift 2;;  --wan-if) WAN_IF=$2; shift 2;;
    --ccc-ip) CCC_IP=$2; shift 2;;  --subnet) SUBNET=$2; shift 2;;
    --lab-name) LAB_NAME=$2; shift 2;; --mode) MODE=$2; shift 2;;
    --yes|-y) YES=1; shift;; -h|--help) sed -n '2,10p' "$0"; exit 0;;
    *) die "unknown option $1";;
  esac
done

[ "$(id -u)" = 0 ] || die "run with sudo"
. /etc/os-release
[ "${ID:-}" = ubuntu ] || warn "tested on Ubuntu 22.04/24.04; continuing on $PRETTY_NAME"

# ---------------------------------------------------------------- network questions
ifaces=$(ls /sys/class/net | grep -v -E '^(lo|docker|veth|br-)' | tr '\n' ' ' || true)
say "Network interfaces: $ifaces"
default_wan=$(ip route show default | awk '{print $5; exit}' || true)
WAN_IF=${WAN_IF:-$(ask "Internet-facing interface (WAN)" "$default_wan")}
default_lan=$(for i in $ifaces; do if [ "$i" != "$WAN_IF" ]; then echo "$i"; break; fi; done)
MODE=$(ask "Network mode (gateway|proxy_only)" "$MODE")
if [ "$MODE" = gateway ]; then
  LAN_IF=${LAN_IF:-$(ask "Lab-facing interface (LAN)" "${default_lan:-}")}
  [ -n "$LAN_IF" ] && [ "$LAN_IF" != "$WAN_IF" ] || die "gateway mode needs two different interfaces"
else
  LAN_IF=${LAN_IF:-$WAN_IF}
fi
cur_ip=$(ip -4 -o addr show dev "$LAN_IF" 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1 || true)
[ -n "$cur_ip" ] || cur_ip=$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for(i=1;i<=NF;i++) if($i=="src") print $(i+1)}' | head -1 || true)
CCC_IP=${CCC_IP:-$(ask "CCC address on the lab network" "${cur_ip:-}")}
SUBNET=${SUBNET:-$(ask "Lab subnet" "$(echo "$CCC_IP" | cut -d. -f1-3).0/24")}
LAB_NAME=$(ask "Lab name" "$LAB_NAME")
TZ_NAME=$(timedatectl show -p Timezone --value 2>/dev/null || echo UTC)
if ! ip -4 addr show dev "$LAN_IF" | grep -q "inet $CCC_IP/"; then
  warn "$CCC_IP is not configured on $LAN_IF. Give the CCC a fixed address, e.g. /etc/netplan/60-xephyguard.yaml:"
  cat <<NP
    network:
      version: 2
      ethernets:
        $LAN_IF: { addresses: [$CCC_IP/${SUBNET#*/}], dhcp4: false }
NP
  [ "$YES" = 1 ] || read -r -p "Press Enter to continue anyway, Ctrl+C to stop... " _
fi

# ---------------------------------------------------------------- packages
say "Installing Docker, nftables and tools"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq docker.io docker-compose-v2 nftables openssl curl dnsutils chrony jq >/dev/null
mkdir -p /etc/docker
# Docker must not touch the firewall: XephyGuard owns forwarding via nftables, and all CCC
# containers use host networking, so Docker's NAT/iptables rules are not needed.
if ! grep -q '"iptables": false' /etc/docker/daemon.json 2>/dev/null; then
  echo '{ "iptables": false, "ip6tables": false, "log-driver": "json-file", "log-opts": { "max-size": "20m", "max-file": "5" } }' > /etc/docker/daemon.json
  systemctl restart docker
fi
systemctl enable --now docker chrony >/dev/null

# ---------------------------------------------------------------- host networking
say "Configuring the host (IP forwarding, DNS port 53)"
echo 'net.ipv4.ip_forward=1' > /etc/sysctl.d/60-xephyguard.conf
sysctl -q -p /etc/sysctl.d/60-xephyguard.conf
if systemctl is-active -q systemd-resolved; then
  mkdir -p /etc/systemd/resolved.conf.d
  printf '[Resolve]\nDNSStubListener=no\n' > /etc/systemd/resolved.conf.d/xephyguard.conf
  systemctl restart systemd-resolved
  ln -sf /run/systemd/resolve/resolv.conf /etc/resolv.conf
  systemctl restart docker      # dockerd caches resolv.conf: it must not keep pointing at the disabled stub
fi

# ---------------------------------------------------------------- state + secrets
say "Preparing $STATE"
install -d -m 0755 "$STATE" "$STATE/mysql"
install -d -m 0755 -o 33 -g 33 "$STATE/net" "$STATE/storage" /var/log/xephyguard   # web app runs as www-data (uid 33) and writes here
install -d -m 0711 "$STATE/ca"
install -d -m 0700 -o 1500 -g 1500 "$STATE/mitmproxy"          # proxy container runs as uid 1500
install -d -m 0750 -o 1500 -g 1500 /var/log/xephyguard/proxy
ENV="$DEPLOY/.env"
rand() { openssl rand -hex "${1:-24}"; }
if [ ! -f "$ENV" ]; then
  cat > "$ENV" <<ENVF
APP_NAME=XephyGuard
APP_ENV=production
APP_KEY=base64:$(openssl rand -base64 32)
APP_DEBUG=false
APP_URL=https://$CCC_IP
APP_TIMEZONE=$TZ_NAME
LOG_CHANNEL=daily
DB_CONNECTION=mysql
DB_HOST=127.0.0.1
DB_PORT=3306
DB_DATABASE=xephyguard
DB_USERNAME=xephyguard
DB_PASSWORD=$(rand 16)
DB_ROOT_PASSWORD=$(rand 16)
SESSION_DRIVER=database
SESSION_SECURE_COOKIE=true
QUEUE_CONNECTION=database
CACHE_STORE=database
BROADCAST_CONNECTION=reverb
REVERB_APP_ID=xephyguard
REVERB_APP_KEY=$(rand 10)
REVERB_APP_SECRET=$(rand 24)
REVERB_HOST=127.0.0.1
REVERB_PORT=8081
REVERB_SCHEME=http
XG_LAB_NAME="$LAB_NAME"
XG_CCC_IP=$CCC_IP
XG_LAB_SUBNET=$SUBNET
XG_LAN_IF=$LAN_IF
XG_WAN_IF=$WAN_IF
XG_NETWORK_MODE=$MODE
XG_CA_DIR=$STATE/ca
XG_NET_STATE=$STATE/net/state.json
XG_INTERNAL_TOKEN=$(rand 24)
XG_INTERNAL_URL=http://127.0.0.1:8000
XG_ANALYSIS_URL=http://127.0.0.1:8090
XG_REQUIRE_MTLS=true
SCREEN_GATEWAY_ADDR=:8082
SCREEN_GATEWAY_PORT=8082
SCREEN_GATEWAY_PATH=/screen-gw
SCREEN_GATEWAY_SECRET=$(rand 32)
SCREEN_GATEWAY_AGENT_SECRET=$(rand 32)
SCREEN_GATEWAY_ISSUER=xephyguard-ccc
SCREEN_GATEWAY_AUDIENCE=xephyguard-gateway
SCREEN_GATEWAY_URL=
ENVF
  chmod 0600 "$ENV"
else
  say "Keeping existing secrets in $ENV"
fi
set -a; . "$ENV"; set +a

# ---------------------------------------------------------------- images, database, CA
cd "$DEPLOY"
say "Building images (first run takes a few minutes)"
docker compose build -q
docker compose up -d mysql
say "Waiting for MySQL"
for _ in $(seq 60); do docker compose exec -T mysql mysqladmin ping -h 127.0.0.1 -uroot -p"$DB_ROOT_PASSWORD" >/dev/null 2>&1 && break; sleep 2; done
artisan() { docker compose run --rm -T web artisan "$@"; }
artisan migrate --force
say "Creating the lab certificate authority"
artisan xephyguard:ca:init --server-ip="$CCC_IP" --server-dns=xephyguard-ccc --server-dns="$(hostname)"
chown 33:33 "$STATE/ca"/ca.key "$STATE/ca"/server.key && chmod 0600 "$STATE/ca"/*.key && chmod 0644 "$STATE/ca"/*.crt
# The transparent proxy signs on-the-fly certificates with the lab CA (key never leaves the CCC).
cat "$STATE/ca/ca.key" "$STATE/ca/ca.crt" > "$STATE/mitmproxy/mitmproxy-ca.pem"
cp "$STATE/ca/ca.crt" "$STATE/mitmproxy/mitmproxy-ca-cert.pem"
chown 1500:1500 "$STATE/mitmproxy"/mitmproxy-ca*.pem && chmod 0600 "$STATE/mitmproxy/mitmproxy-ca.pem"
artisan xephyguard:bootstrap

# ---------------------------------------------------------------- firewall
if [ "$MODE" = gateway ]; then
  say "Installing nftables gateway rules ($LAN_IF -> $WAN_IF)"
  install -d /etc/nftables.d
  sed -e "s|@LAN_IF@|$LAN_IF|g; s|@WAN_IF@|$WAN_IF|g; s|@CCC_IP@|$CCC_IP|g; s|@LAB_SUBNET@|$SUBNET|g; s|@PROXY_PORT@|8080|g; s|@AGENT_PORT@|8443|g" \
      "$ROOT/proxy/nftables/xephyguard.nft" > /etc/nftables.d/xephyguard.nft
  grep -q 'nftables.d/xephyguard.nft' /etc/nftables.conf || echo 'include "/etc/nftables.d/xephyguard.nft"' >> /etc/nftables.conf
  nft -c -f /etc/nftables.d/xephyguard.nft
  nft -f /etc/nftables.d/xephyguard.nft
  systemctl enable nftables >/dev/null
else
  warn "proxy_only mode: configure the lab router to forward TCP 80/443 to $CCC_IP:8080 and to hand out $CCC_IP as DNS."
fi

# ---------------------------------------------------------------- start
say "Starting XephyGuard"
docker compose up -d
install -m 0755 "$DEPLOY/bin/xephyguard-ccc" /usr/local/sbin/xephyguard-ccc
sed -i "s|^XG_DEPLOY=.*|XG_DEPLOY=$DEPLOY|" /usr/local/sbin/xephyguard-ccc
sleep 5
FP=$(openssl x509 -in "$STATE/ca/ca.crt" -noout -fingerprint -sha256 | cut -d= -f2 | tr -d ':' | tr 'A-F' 'a-f')
cat <<DONE

  XephyGuard CCC is running.

  Dashboard:        https://$CCC_IP/        (first visit opens the setup wizard)
  CA fingerprint:   $FP
  Health check:     sudo xephyguard-ccc doctor

  Browsers will warn about the certificate until the lab CA is trusted on this admin machine:
  download http://$CCC_IP/ca.crt and compare the fingerprint above.

DONE
