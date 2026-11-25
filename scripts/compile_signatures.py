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
    for r in bundle.get("extensions", []):
        for key in ("domains",):
            if key in r and not all(isinstance(d, str) and "." in d for d in r[key]):
                errors.append(f"{r.get('id')}: {key} must be host names")
        for ext, doms in (r.get("id_domains") or {}).items():
            if not isinstance(doms, list) or not all(isinstance(d, str) and "." in d for d in doms):
                errors.append(f"{r.get('id')}: id_domains.{ext} must be a list of host names")
    errors += validate_web(bundle.get("web", {}))
    return errors


def validate_web(web: dict) -> list[str]:
    errors = []
    patterns = ("equals", "prefix", "suffix", "contains")
    for s in web.get("ai_sites", []):
        if not s.get("site"):
            errors.append("web.ai_sites: entry without site")
        if not any(s.get(k) for k in patterns):
            errors.append(f"web.ai_sites.{s.get('site')}: needs equals/prefix/suffix/contains")
        for k in patterns:
            if any(not isinstance(v, str) or not v.strip() for v in s.get(k, [])):
                errors.append(f"web.ai_sites.{s.get('site')}.{k}: empty or non-string pattern")
    for key in ("browsers", "browser_title_suffixes", "telemetry_domains"):
        if not all(isinstance(v, str) and v for v in web.get(key, [])):
            errors.append(f"web.{key}: must be non-empty strings")
    if web.get("ai_tab_severity", "medium") not in ("info", "low", "medium", "high", "critical"):
        errors.append("web.ai_tab_severity: bad severity")
    for proc, dom in (web.get("process_domains") or {}).items():
        if not isinstance(dom, str) or "." not in dom:
            errors.append(f"web.process_domains.{proc}: must be a host name")
    tunnel = web.get("tunnel")
    if tunnel is not None:
        if not isinstance(tunnel, dict) or not str(tunnel.get("id", "")).startswith("net."):
            errors.append("web.tunnel: needs an id starting with 'net.'")
        elif tunnel.get("severity", "medium") not in ("info", "low", "medium", "high", "critical"):
            errors.append("web.tunnel.severity: bad severity")
        else:
            ports = tunnel.get("tcp_ports", [])
            if not ports or not all(isinstance(x, int) and 0 < x < 65536 and x not in (80, 443) for x in ports):
                errors.append("web.tunnel.tcp_ports: 1-65535, never 80/443 (normal web traffic)")
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
        "web": {k: v for k, v in load(src, "web").items() if k != "version"},
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
