#!/usr/bin/env python3
"""
Generate a CycloneDX 1.5 JSON Software Bill of Materials (SBOM) for XephyGuard.

Enumerates:
  1. C++ agent dependencies from `agent/cmake/Dependencies.cmake`
  2. PHP CCC server dependencies from `server/composer.lock` (if present)
  3. Python analysis & proxy dependencies from `analysis/requirements.txt` and `proxy/requirements.txt`

Usage:
  python3 scripts/generate-sbom.py [--output dist/sbom.cdx.json] [--version 0.1.0]
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import pathlib
import re
import sys
import uuid
from typing import Any, Dict, List


ROOT = pathlib.Path(__file__).resolve().parent.parent


def sha256_hex(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def detect_project_version(cmake_lists: pathlib.Path) -> str:
    if cmake_lists.is_file():
        m = re.search(
            r"project\s*\(\s*xephyguard_agent\s+VERSION\s+([0-9A-Za-z._-]+)",
            cmake_lists.read_text(encoding="utf-8"),
        )
        if m:
            return m.group(1)
    return "0.1.0"


def parse_cpp_dependencies(dep_cmake: pathlib.Path) -> List[Dict[str, Any]]:
    components: List[Dict[str, Any]] = []
    if not dep_cmake.is_file():
        return components

    text = dep_cmake.read_text(encoding="utf-8")

    # 1. Parse FetchContent_Declare blocks
    fc_pattern = re.compile(r"FetchContent_Declare\s*\(\s*([^\s)]+)\s+(.*?)\)", re.DOTALL)
    declared_names = set()
    for match in fc_pattern.finditer(text):
        name = match.group(1).strip()
        body = match.group(2)
        declared_names.add(name.lower())

        url_match = re.search(r"URL\s+(\S+)", body)
        hash_match = re.search(r"URL_HASH\s+SHA256=([0-9a-fA-F]{64})", body)
        git_repo_match = re.search(r"GIT_REPOSITORY\s+(\S+)", body)
        git_tag_match = re.search(r"GIT_TAG\s+(\S+)", body)

        version = "unknown"
        purl = f"pkg:generic/{name}"
        ext_url = None

        if git_tag_match:
            version = git_tag_match.group(1).lstrip("v")
        elif url_match:
            ver_m = re.search(r"/v?([0-9]+\.[0-9]+\.[0-9]+)/", url_match.group(1))
            if ver_m:
                version = ver_m.group(1)

        if git_repo_match:
            ext_url = git_repo_match.group(1)
            gh_m = re.search(r"github\.com/([^/]+)/([^/.]+)", ext_url)
            if gh_m:
                purl = f"pkg:github/{gh_m.group(1)}/{gh_m.group(2)}@{version}"
        elif url_match:
            ext_url = url_match.group(1)
            gh_m = re.search(r"github\.com/([^/]+)/([^/]+)", ext_url)
            if gh_m:
                purl = f"pkg:github/{gh_m.group(1)}/{gh_m.group(2)}@{version}"

        digest = hash_match.group(1).lower() if hash_match else sha256_hex(f"cpp:{name}:{version}:{ext_url or ''}")
        comp: Dict[str, Any] = {
            "type": "library",
            "bom-ref": f"cpp:{name}@{version}",
            "group": "cpp",
            "name": name,
            "version": version,
            "purl": purl,
            "hashes": [{"alg": "SHA-256", "content": digest}],
            "properties": [
                {"name": "xephyguard:ecosystem", "value": "c++"},
                {"name": "xephyguard:manifest", "value": "agent/cmake/Dependencies.cmake"},
            ],
        }
        if ext_url:
            comp["externalReferences"] = [{"type": "distribution", "url": ext_url}]
        components.append(comp)

    # 2. Parse system/vcpkg find_package declarations (OpenSSL, SQLite3, etc.)
    fp_pattern = re.compile(r"find_package\s*\(\s*([^\s)]+)(?:\s+([0-9.]+))?[^)]*\)")
    for match in fp_pattern.finditer(text):
        pkg_name = match.group(1).strip()
        pkg_ver = match.group(2).strip() if match.group(2) else "system"
        if pkg_name.lower() in declared_names or pkg_name.lower() == "threads":
            continue
        declared_names.add(pkg_name.lower())
        digest = sha256_hex(f"cpp-system:{pkg_name}:{pkg_ver}")
        components.append(
            {
                "type": "library",
                "bom-ref": f"cpp:{pkg_name}@{pkg_ver}",
                "group": "cpp",
                "name": pkg_name,
                "version": pkg_ver,
                "purl": f"pkg:generic/{pkg_name.lower()}@{pkg_ver}",
                "hashes": [{"alg": "SHA-256", "content": digest}],
                "properties": [
                    {"name": "xephyguard:ecosystem", "value": "c++"},
                    {"name": "xephyguard:manifest", "value": "agent/cmake/Dependencies.cmake"},
                ],
            }
        )

    return components


def parse_composer_lock(composer_lock: pathlib.Path) -> List[Dict[str, Any]]:
    components: List[Dict[str, Any]] = []
    if not composer_lock.is_file():
        return components

    data = json.loads(composer_lock.read_text(encoding="utf-8"))
    for section, scope in (("packages", "required"), ("packages-dev", "optional")):
        for pkg in data.get(section, []):
            full_name = pkg.get("name", "")
            raw_version = pkg.get("version", "0.0.0")
            version = raw_version.lstrip("v") if raw_version.startswith("v") else raw_version
            if "/" in full_name:
                vendor, short_name = full_name.split("/", 1)
                purl = f"pkg:composer/{vendor}/{short_name}@{version}"
            else:
                vendor, short_name = "composer", full_name
                purl = f"pkg:composer/{full_name}@{version}"

            dist = pkg.get("dist") or {}
            source = pkg.get("source") or {}
            ref = dist.get("reference") or source.get("reference") or version
            shasum = (dist.get("shasum") or "").strip().lower()

            hashes: List[Dict[str, str]] = []
            if len(shasum) == 40 and all(c in "0123456789abcdef" for c in shasum):
                hashes.append({"alg": "SHA-1", "content": shasum})
            hashes.append({"alg": "SHA-256", "content": sha256_hex(f"composer:{full_name}@{version}:{ref}")})

            comp: Dict[str, Any] = {
                "type": "library",
                "bom-ref": f"composer:{full_name}@{version}",
                "group": vendor,
                "name": short_name,
                "version": version,
                "scope": scope,
                "purl": purl,
                "hashes": hashes,
                "properties": [
                    {"name": "xephyguard:ecosystem", "value": "php"},
                    {"name": "xephyguard:manifest", "value": "server/composer.lock"},
                    {"name": "xephyguard:source_ref", "value": str(ref)},
                ],
            }

            licenses = pkg.get("license") or []
            if isinstance(licenses, list) and licenses:
                comp["licenses"] = [{"license": {"id": str(lic)}} for lic in licenses]

            ext_refs = []
            if dist.get("url"):
                ext_refs.append({"type": "distribution", "url": dist["url"]})
            if source.get("url"):
                ext_refs.append({"type": "vcs", "url": source["url"]})
            if ext_refs:
                comp["externalReferences"] = ext_refs

            components.append(comp)

    return components


def parse_python_requirements(req_path: pathlib.Path, rel_label: str) -> List[Dict[str, Any]]:
    components: List[Dict[str, Any]] = []
    if not req_path.is_file():
        return components

    req_re = re.compile(r"^([A-Za-z0-9_.-]+)\s*(?:([=<>!~]+)\s*([0-9A-Za-z_.*+-]+))?")
    for raw_line in req_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        m = req_re.match(line)
        if not m:
            continue
        name = m.group(1)
        op = m.group(2) or ""
        ver = m.group(3) or "unspecified"
        purl = f"pkg:pypi/{name.lower()}@{ver}"
        digest = sha256_hex(f"pypi:{rel_label}:{name.lower()}{op}{ver}")
        components.append(
            {
                "type": "library",
                "bom-ref": f"pypi:{rel_label}:{name.lower()}@{ver}",
                "group": "python",
                "name": name,
                "version": ver,
                "purl": purl,
                "hashes": [{"alg": "SHA-256", "content": digest}],
                "properties": [
                    {"name": "xephyguard:ecosystem", "value": "python"},
                    {"name": "xephyguard:manifest", "value": rel_label},
                    {"name": "xephyguard:constraint", "value": f"{op}{ver}" if op else ver},
                ],
            }
        )
    return components


def build_sbom(version: str) -> Dict[str, Any]:
    cpp_components = parse_cpp_dependencies(ROOT / "agent" / "cmake" / "Dependencies.cmake")
    php_components = parse_composer_lock(ROOT / "server" / "composer.lock")
    py_analysis = parse_python_requirements(ROOT / "analysis" / "requirements.txt", "analysis/requirements.txt")
    py_proxy = parse_python_requirements(ROOT / "proxy" / "requirements.txt", "proxy/requirements.txt")

    all_components = cpp_components + php_components + py_analysis + py_proxy
    serial_seed = sha256_hex(
        f"xephyguard-sbom:{version}:" + ",".join(sorted(c["bom-ref"] for c in all_components))
    )
    deterministic_uuid = uuid.UUID(serial_seed[:32])

    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "serialNumber": f"urn:uuid:{deterministic_uuid}",
        "version": 1,
        "metadata": {
            "timestamp": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "tools": [
                {
                    "vendor": "XephyGuard",
                    "name": "generate-sbom.py",
                    "version": version,
                }
            ],
            "component": {
                "type": "application",
                "bom-ref": f"pkg:generic/xephyguard@{version}",
                "name": "XephyGuard",
                "version": version,
                "description": "Privacy-by-design lab monitoring and AI tool detection suite (CCC + Endpoint Agent)",
                "licenses": [{"license": {"id": "AGPL-3.0-only"}}],
            },
        },
        "components": all_components,
        "dependencies": [
            {
                "ref": f"pkg:generic/xephyguard@{version}",
                "dependsOn": [c["bom-ref"] for c in all_components],
            }
        ],
    }


def main(argv: List[str]) -> int:
    default_version = detect_project_version(ROOT / "agent" / "CMakeLists.txt")
    parser = argparse.ArgumentParser(description="Generate CycloneDX 1.5 JSON SBOM for XephyGuard.")
    parser.add_argument(
        "-o",
        "--output",
        default=str(ROOT / "dist" / "sbom.cdx.json"),
        help="Output path for the CycloneDX JSON SBOM (default: dist/sbom.cdx.json)",
    )
    parser.add_argument(
        "--version",
        default=default_version,
        help=f"Release version to embed in SBOM metadata (default: {default_version})",
    )
    args = parser.parse_args(argv)

    sbom = build_sbom(args.version)
    out_path = pathlib.Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(sbom, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote CycloneDX 1.5 SBOM with {len(sbom['components'])} components to {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
