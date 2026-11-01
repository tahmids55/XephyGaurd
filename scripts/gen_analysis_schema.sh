#!/usr/bin/env bash
# Regenerates analysis/tests/schema.sql from the Laravel migrations so the Python analysis tests
# always run against the real schema.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
touch "$tmp/s.sqlite"
(cd "$root/server" && DB_CONNECTION=sqlite DB_DATABASE="$tmp/s.sqlite" php artisan migrate:fresh --force >/dev/null)
python3 - "$tmp/s.sqlite" > "$root/analysis/tests/schema.sql" <<'PY'
import sqlite3, sys
c = sqlite3.connect(sys.argv[1])
print("-- Generated from the Laravel migrations (server/database/migrations) via SQLite. Regenerate with scripts/gen_analysis_schema.sh")
for (sql,) in c.execute("select sql from sqlite_master where sql is not null and name not like 'sqlite_%' order by type desc, name"):
    print(sql + ";")
PY
echo "wrote analysis/tests/schema.sql"
