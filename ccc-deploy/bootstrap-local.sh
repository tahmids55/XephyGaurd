#!/usr/bin/env bash
set -euo pipefail

DEPLOY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$DEPLOY_DIR/.." && pwd)"
ENV_FILE="$DEPLOY_DIR/.env.local"
COMPOSE_FILE="$DEPLOY_DIR/docker-compose.local.yml"

if [ ! -f "$ENV_FILE" ]; then
    echo "Missing $ENV_FILE" >&2
    exit 1
fi

set -a
# shellcheck source=/dev/null
. "$ENV_FILE"
set +a

dc() {
    docker compose --env-file "$ENV_FILE" -f "$COMPOSE_FILE" "$@"
}

RESEED="${1:-}"

echo "[1/6] Starting MySQL..."
dc up -d mysql
for _ in $(seq 60); do
    if dc exec -T mysql mysqladmin ping -h 127.0.0.1 -uroot -p"$DB_ROOT_PASSWORD" >/dev/null 2>&1; then
        break
    fi
    sleep 2
done
dc exec -T mysql mysqladmin ping -h 127.0.0.1 -uroot -p"$DB_ROOT_PASSWORD" >/dev/null

echo "[2/6] Seeding CA, mitmproxy CA, and downloadable artifacts into Compose volumes..."
dc run --rm -T --no-deps --entrypoint sh \
    -v "$REPO_ROOT/server/storage/app/private/ca:/src/ca:ro" \
    -v "$REPO_ROOT/server/storage/app/public/downloads:/src/downloads:ro" \
    web -eu -c '
        mkdir -p /var/lib/xephyguard/ca /var/www/html/storage/app/public/downloads /var/www/html/storage/app/private/ca /var/www/html/storage/app/net
        cp -a /src/ca/. /var/lib/xephyguard/ca/
        chmod 0711 /var/lib/xephyguard/ca
        chmod 0644 /var/lib/xephyguard/ca/*.crt /var/lib/xephyguard/ca/*.pem 2>/dev/null || true
        chown 33:33 /var/lib/xephyguard/ca/*.key 2>/dev/null || true
        chmod 0600 /var/lib/xephyguard/ca/*.key 2>/dev/null || true

        cp -a /src/downloads/. /var/www/html/storage/app/public/downloads/
        chown -R 33:33 /var/www/html/storage/app
    '

dc run --rm -T --no-deps --user 0:0 --entrypoint sh \
    -v "$REPO_ROOT/server/storage/app/private/ca:/src/ca:ro" \
    proxy -eu -c '
        mkdir -p /home/mitmproxy/.mitmproxy
        if [ -f /src/ca/ca.key ] && [ -f /src/ca/ca.crt ]; then
            cat /src/ca/ca.key /src/ca/ca.crt > /home/mitmproxy/.mitmproxy/mitmproxy-ca.pem
            cp /src/ca/ca.crt /home/mitmproxy/.mitmproxy/mitmproxy-ca-cert.pem
        elif [ -f /src/ca/mitmproxy-ca.pem ]; then
            cp /src/ca/mitmproxy-ca.pem /home/mitmproxy/.mitmproxy/mitmproxy-ca.pem
            if [ -f /src/ca/interception-ca.crt ]; then
                cp /src/ca/interception-ca.crt /home/mitmproxy/.mitmproxy/mitmproxy-ca-cert.pem
            else
                cp /src/ca/mitmproxy-ca.pem /home/mitmproxy/.mitmproxy/mitmproxy-ca-cert.pem
            fi
        fi
        chown -R 1500:1500 /home/mitmproxy/.mitmproxy
        chmod 0700 /home/mitmproxy/.mitmproxy
        chmod 0600 /home/mitmproxy/.mitmproxy/mitmproxy-ca.pem
        chmod 0644 /home/mitmproxy/.mitmproxy/mitmproxy-ca-cert.pem
    '

echo "[3/6] Running Laravel migrations on MySQL..."
dc run --rm -T --no-deps web artisan migrate --force

USER_COUNT=$(dc exec -T mysql mysql -N -B -u"$DB_USERNAME" -p"$DB_PASSWORD" "$DB_DATABASE" -e "SELECT COUNT(*) FROM users;")
if [ "$RESEED" = "--reseed" ] || [ "$USER_COUNT" -eq 0 ]; then
    echo "[4/6] Importing server/database/database.sqlite into MySQL..."
    dc run --rm -T --no-deps \
        -v "$REPO_ROOT/server/database/database.sqlite:/tmp/import.sqlite:ro" \
        -v "$DEPLOY_DIR/import-sqlite-to-local.php:/tmp/import-sqlite-to-local.php:ro" \
        --entrypoint php web /tmp/import-sqlite-to-local.php /tmp/import.sqlite
else
    echo "[4/6] MySQL already contains $USER_COUNT user(s); skipping SQLite import (pass --reseed to overwrite)."
fi

echo "[5/6] Generating network policy state..."
dc run --rm -T --no-deps web artisan xephyguard:net:write
dc run --rm -T --no-deps --entrypoint sh web -c 'chown -R 33:33 /var/lib/xephyguard/net'

echo "[6/6] Starting full proxy_only + screen-gateway stack..."
dc up -d
dc ps
