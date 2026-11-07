#!/usr/bin/env bash
# Builds a release: Linux agent .deb, Windows installer (if a Windows build is present), CCC
# Compose bundle, checksums and a version manifest. Optionally signs the checksums with the CCC CA
# key so agents/admins can verify them (--sign-key /var/lib/xephyguard/ca/ca.key).
#
#   scripts/release.sh 0.1.0 [--sign-key PATH] [--publish-to /var/lib/xephyguard/storage/public/downloads]
set -euo pipefail
VER=${1:?usage: release.sh VERSION [--sign-key PATH] [--publish-to DIR]}; shift
SIGN="" PUB=""
while [ $# -gt 0 ]; do case "$1" in --sign-key) SIGN=$2; shift 2;; --publish-to) PUB=$2; shift 2;; *) echo "unknown $1"; exit 2;; esac; done
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/dist/$VER"
rm -rf "$OUT" && mkdir -p "$OUT"

echo "==> Linux agent"
sed -i "s/project(xephyguard_agent VERSION [0-9.]*/project(xephyguard_agent VERSION $VER/" "$ROOT/agent/CMakeLists.txt"
cmake -S "$ROOT/agent" -B "$ROOT/agent/build-release" -DCMAKE_BUILD_TYPE=Release -DXG_BUILD_TESTS=OFF >/dev/null
cmake --build "$ROOT/agent/build-release" -j"$(nproc)"
(cd "$ROOT/agent/build-release" && rm -f ./*.deb && cpack -G DEB >/dev/null)
cp "$ROOT"/agent/build-release/xephyguard-agent_"$VER"_*.deb "$OUT/"
arch=$(dpkg --print-architecture)
cp "$OUT/xephyguard-agent_${VER}_${arch}.deb" "$OUT/xephyguard-agent_latest_${arch}.deb"

echo "==> Windows installer"
WIN_BUILD="$ROOT/agent/build-windows/Release"
if [ -f "$WIN_BUILD/xephyguard-agent.exe" ] && command -v makensis >/dev/null; then
  python3 "$ROOT/scripts/compile_signatures.py" --out "$ROOT/installers/windows/signatures.json"
  (cd "$ROOT/installers/windows" && makensis -V2 -DVERSION="$VER" -DBUILD_DIR="$WIN_BUILD" xephyguard.nsi)
  mv "$ROOT/installers/windows/XephyGuard-Setup-$VER.exe" "$OUT/"
  cp "$OUT/XephyGuard-Setup-$VER.exe" "$OUT/XephyGuard-Setup-latest.exe"
else
  echo "    (skipped: build the agent with MSVC into agent/build-windows and install nsis)"
fi

echo "==> CCC bundle"
tar -C "$ROOT" --exclude='server/vendor' --exclude='server/node_modules' --exclude='server/.env' \
    --exclude='server/storage/app/*' --exclude='server/database/*.sqlite' --exclude='ccc-deploy/.env' \
    -czf "$OUT/xephyguard-ccc-$VER.tar.gz" server ccc-deploy proxy analysis data scripts eval docs

echo "==> CycloneDX 1.5 SBOM"
python3 "$ROOT/scripts/generate-sbom.py" --version "$VER" --output "$OUT/sbom.cdx.json"
cp "$OUT/sbom.cdx.json" "$ROOT/dist/sbom.cdx.json"

echo "==> Checksums and manifest"
(cd "$OUT" && sha256sum ./* > SHA256SUMS)
python3 - "$OUT" "$VER" <<'PY'
import json, pathlib, sys, hashlib, datetime
out, ver = pathlib.Path(sys.argv[1]), sys.argv[2]
files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.iterdir()) if p.name not in ("SHA256SUMS", "manifest.json")}
(out / "manifest.json").write_text(json.dumps({"version": ver, "built": datetime.datetime.utcnow().isoformat() + "Z", "files": files}, indent=1))
PY
if [ -n "$SIGN" ]; then
  openssl dgst -sha256 -sign "$SIGN" -out "$OUT/SHA256SUMS.sig" "$OUT/SHA256SUMS"
  echo "    signed: verify with  openssl dgst -sha256 -verify <(openssl x509 -in ca.crt -pubkey -noout) -signature SHA256SUMS.sig SHA256SUMS"
fi
if [ -n "$PUB" ]; then
  mkdir -p "$PUB" && cp "$OUT"/*.deb "$OUT"/*.exe "$OUT"/sbom.cdx.json "$OUT"/SHA256SUMS* "$PUB"/ 2>/dev/null || true
  echo "    published installers and SBOM to $PUB (served at http://<ccc>/downloads/)"
fi
ls -la "$OUT"
