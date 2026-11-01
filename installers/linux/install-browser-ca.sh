#!/bin/sh
# Adds the lab CA to the per-user NSS databases used by Chromium-based browsers (Firefox uses the
# enterprise policy instead). Runs at install and can be re-run by the agent for new users.
CA=/etc/xephyguard/ca.crt
command -v certutil >/dev/null || { echo "libnss3-tools not installed; skipping NSS import"; exit 0; }
for home in /home/* /etc/skel; do
  db="$home/.pki/nssdb"
  mkdir -p "$db"
  [ -f "$db/cert9.db" ] || certutil -d "sql:$db" -N --empty-password
  certutil -d "sql:$db" -D -n "XephyGuard Lab CA" >/dev/null 2>&1 || true
  certutil -d "sql:$db" -A -t "C,," -n "XephyGuard Lab CA" -i "$CA"
  [ "$home" = /etc/skel ] || chown -R "$(stat -c %U "$home")": "$home/.pki"
done
