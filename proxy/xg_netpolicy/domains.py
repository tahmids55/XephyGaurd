"""Domain and path matching shared by the DNS generator and the proxy addon."""
from __future__ import annotations

import ipaddress
import re
from dataclasses import dataclass, field
from typing import Iterable

_LABEL = re.compile(r"^[a-z0-9_]([a-z0-9_-]{0,61}[a-z0-9_])?$")


def normalize_domain(d: str) -> str | None:
    """Lowercase, strip scheme/port/trailing dot/wildcards. Returns None if invalid."""
    d = d.strip().lower()
    if not d or d.startswith("#"):
        return None
    d = re.sub(r"^[a-z]+://", "", d)
    d = d.split("/")[0].split(":")[0].strip(".")
    if d.startswith("*."):
        d = d[2:]
    if not d or "." not in d:
        return None
    try:
        ipaddress.ip_address(d)
        return None  # IPs are not domains
    except ValueError:
        pass
    if not all(_LABEL.match(p) for p in d.split(".")):
        return None
    return d


def parse_list(text: str) -> tuple[list[str], list[str]]:
    """Parse a blocklist file: returns (domains, path_rules). Supports '#' comments, tags in [..],
    hosts-file format ('0.0.0.0 example.com') and 'host/path' path rules."""
    domains: list[str] = []
    paths: list[str] = []
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        tokens = [t for t in line.split() if not t.startswith("[")]
        if not tokens:
            continue
        if len(tokens) >= 2 and tokens[0] in ("0.0.0.0", "127.0.0.1", "::"):
            tokens = tokens[1:]
        entry = tokens[0].lower()
        entry = re.sub(r"^[a-z]+://", "", entry)
        if "/" in entry.strip("/"):
            host, _, path = entry.partition("/")
            if normalize_domain(host):
                paths.append(f"{normalize_domain(host)}/{path.strip('/')}")
            continue
        d = normalize_domain(entry)
        if d:
            domains.append(d)
    return sorted(set(domains)), sorted(set(paths))


@dataclass
class DomainSet:
    """Suffix matcher: 'openai.com' matches 'openai.com' and 'api.openai.com', not 'notopenai.com'."""

    domains: set[str] = field(default_factory=set)

    @classmethod
    def of(cls, items: Iterable[str]) -> "DomainSet":
        return cls({d for d in (normalize_domain(i) for i in items) if d})

    def matches(self, host: str | None) -> str | None:
        if not host:
            return None
        h = host.lower().rstrip(".")
        parts = h.split(".")
        for i in range(len(parts) - 1):
            cand = ".".join(parts[i:])
            if cand in self.domains:
                return cand
        return None

    def __len__(self) -> int:
        return len(self.domains)


@dataclass
class PathRules:
    """'huggingface.co/chat' blocks https://huggingface.co/chat and below (and subdomains of the host)."""

    rules: list[tuple[str, str]] = field(default_factory=list)

    @classmethod
    def of(cls, items: Iterable[str]) -> "PathRules":
        out = []
        for it in items:
            host, _, path = it.lower().partition("/")
            if host:
                out.append((host, "/" + path.strip("/")))
        return cls(out)

    def matches(self, host: str | None, path: str) -> str | None:
        if not host:
            return None
        h = host.lower().rstrip(".")
        p = path.split("?", 1)[0].lower() or "/"
        for rh, rp in self.rules:
            if (h == rh or h.endswith("." + rh)) and (p == rp or p.startswith(rp.rstrip("/") + "/")):
                return f"{rh}{rp}"
        return None
