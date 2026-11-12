#!/usr/bin/env python3
"""Compile data/signatures/*.yaml into the JSON bundle format the agent understands.

The CCC does the same thing (server/app/Services/SignatureCompiler.php) and additionally signs the
bundle with the internal CA key. This script produces the *unsigned seed bundle* shipped inside the
agent packages, so a freshly installed agent has signatures before it first reaches the CCC.

Usage: compile_signatures.py [--src data/signatures] [--out signatures.json] [--version X]
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import sys

try:
    import yaml
except ImportError:  # pragma: no cover
    sys.exit("PyYAML is required: pip install pyyaml")


def load(src: pathlib.Path, name: str) -> dict:
    p = src / f"{name}.yaml"
    if not p.exists():
        return {}
    return yaml.safe_load(p.read_text()) or {}


def validate(bundle: dict) -> list[str]:
    errors, seen = [], set()
    for section in ("processes", "extensions", "adapters", "devices"):
        for r in bundle.get(section, []):
            if not r.get("id"):
                errors.append(f"{section}: rule without id")
            elif r["id"] in seen:
                errors.append(f"duplicate rule id {r['id']}")
            seen.add(r.get("id"))
            if r.get("severity", "medium") not in ("info", "low", "medium", "high", "critical"):
                errors.append(f"{r.get('id')}: bad severity")
    for r in bundle["ports"]["rules"] + bundle["artifacts"]["rules"]:
        if r["id"] in seen:
            errors.append(f"duplicate rule id {r['id']}")
        seen.add(r["id"])
    return errors


def load_domain_list(path: pathlib.Path) -> list[str]:
    if not path.exists():
        return []
    domains: list[str] = []
    seen: set[str] = set()
    for raw in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        token = line.split()[0].lower().lstrip("*.")
        if token in ("0.0.0.0", "127.0.0.1", "::"):
            parts = line.split()
            token = parts[1].lower().lstrip("*.") if len(parts) > 1 else ""
        if not token or "/" in token or "." not in token:
            continue
        if token not in seen:
            seen.add(token)
            domains.append(token)
    return sorted(domains)


def compile_bundle(src: pathlib.Path, version: str | None = None) -> dict:
    ports = load(src, "ports")
    arts = load(src, "artifacts")
    blocklists = src.parent / "blocklists"
    bundle = {
        "processes": load(src, "processes").get("rules", []),
        "extensions": load(src, "extensions").get("rules", []),
        "ports": {"rules": ports.get("rules", []), "probe": ports.get("probe", {})},
        "artifacts": {"rules": arts.get("rules", []), "scan": arts.get("scan", {})},
        "adapters": load(src, "adapters").get("rules", []),
        "devices": load(src, "devices").get("rules", []),
        "domains": {
            "ai": load_domain_list(blocklists / "ai_domains.txt"),
            "ide": load_domain_list(blocklists / "ide_ai_backends.txt"),
        },
    }
    digest = hashlib.sha256(json.dumps(bundle, sort_keys=True).encode()).hexdigest()[:8]
    bundle["version"] = version or f"{datetime.date.today():%Y.%m.%d}-{digest}"
    return bundle


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=root / "data" / "signatures", type=pathlib.Path)
    ap.add_argument("--out", default="-")
    ap.add_argument("--version")
    a = ap.parse_args()
    bundle = compile_bundle(a.src, a.version)
    errs = validate(bundle)
    if errs:
        print("\n".join(errs), file=sys.stderr)
        return 1
    text = json.dumps(bundle, indent=1, sort_keys=True)
    if a.out == "-":
        print(text)
    else:
        pathlib.Path(a.out).write_text(text)
        n = sum(len(bundle[k]) for k in ("processes", "extensions", "adapters", "devices"))
        n += len(bundle["ports"]["rules"]) + len(bundle["artifacts"]["rules"])
        print(f"wrote {a.out}: version {bundle['version']}, {n} rules", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
