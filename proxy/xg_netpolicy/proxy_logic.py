"""Pure decision logic for the XephyGuard proxy addon (no mitmproxy imports, fully unit-testable)."""
from __future__ import annotations

import json
import logging
import os
import pathlib
import queue
import re
import threading
import time
import urllib.request
from dataclasses import dataclass
from typing import Any

from .domains import DomainSet
from .state import State

log = logging.getLogger("xg-proxy")

GOOGLE_HOST = re.compile(r"^(www\.)?google\.[a-z]{2,3}(\.[a-z]{2})?$")


@dataclass
class TlsDecision:
    action: str               # "block" | "inspect" | "pass"
    category: str | None = None
    matched: str | None = None
    reason: str = ""


class PolicyCache:
    """Hot-reloads proxy.json (checked at most every `interval` seconds)."""

    def __init__(self, path: str | pathlib.Path, interval: float = 2.0):
        self.path = pathlib.Path(path)
        self.interval = interval
        self._mtime = 0.0
        self._checked = 0.0
        self.state = State()
        self._blocked: dict[str | None, DomainSet] = {}
        self._inspect = DomainSet()
        self.reload(force=True)

    def reload(self, force: bool = False) -> None:
        now = time.monotonic()
        if not force and now - self._checked < self.interval:
            return
        self._checked = now
        try:
            m = self.path.stat().st_mtime
        except FileNotFoundError:
            return
        if not force and m == self._mtime:
            return
        try:
            self.state = State.from_dict(json.loads(self.path.read_text()))
        except (ValueError, json.JSONDecodeError) as e:
            log.error("proxy policy %s invalid, keeping previous: %s", self.path, e)
            return
        self._mtime = m
        self._blocked = {}
        self._inspect = DomainSet.of(self.state.inspect_hosts)
        log.info("proxy policy v%s loaded", self.state.version)

    def blocked(self, group: str | None) -> DomainSet:
        if group not in self._blocked:
            self._blocked[group] = self.state.blocked_domains_for_group(group)
        return self._blocked[group]

    # -- decisions ------------------------------------------------------------------------------
    def decide_tls(self, client_ip: str | None, sni: str | None) -> TlsDecision:
        self.reload()
        group = self.state.group_of(client_ip)
        if group == "isolated":
            return TlsDecision("block", "isolated", sni, "PC is isolated")
        if not sni:
            if group == "test":
                return TlsDecision("block", "no_sni", None, "TLS without SNI is not allowed in Test mode")
            return TlsDecision("pass")
        hit = self.blocked(group).matches(sni)
        if hit:
            return TlsDecision("block", self.state.category_of(hit), hit, "domain blocklist")
        if group in ("test", "block_ai") and self._inspect.matches(sni):
            return TlsDecision("inspect", reason="selective inspection host")
        return TlsDecision("pass")

    def decide_http(self, client_ip: str | None, host: str, path: str) -> TlsDecision:
        """For plaintext HTTP and for inspected HTTPS requests."""
        self.reload()
        group = self.state.group_of(client_ip)
        if group == "isolated":
            return TlsDecision("block", "isolated", host, "PC is isolated")
        hit = self.blocked(group).matches(host)
        if hit:
            return TlsDecision("block", self.state.category_of(hit), hit, "domain blocklist")
        rule = self.state.path_rules_for_group(group).matches(host, path)
        if rule:
            return TlsDecision("block", "ai", rule, "path rule")
        return TlsDecision("pass")

    def wants_google_rewrite(self, client_ip: str | None) -> bool:
        return self.state.group_of(client_ip) in ("test", "block_ai") and bool(self.state.google.get("force_udm14", True))


def google_search_rewrite(host: str, path: str, query: list[tuple[str, str]]) -> list[tuple[str, str]] | None:
    """Returns a new query list with udm=14 ("Web" results, no AI Overview) or None if unchanged.

    - Only /search on google.<tld>.
    - Leaves other verticals alone (tbm=isch images, udm=2 images, ...), except udm=50 (AI Mode)
      which is forced to 14.
    """
    if not GOOGLE_HOST.match(host.lower()) or path.split("?", 1)[0] != "/search":
        return None
    keys = {k for k, _ in query}
    if "tbm" in keys:
        return None
    udm = [v for k, v in query if k == "udm"]
    if udm and udm[0] not in ("50",):
        return None
    if udm and udm[0] == "14":
        return None
    new = [(k, v) for k, v in query if k != "udm"]
    new.append(("udm", "14"))
    return new


def hide_css(selectors: list[str]) -> str:
    sel = ",".join(s for s in selectors if s and "<" not in s and "{" not in s)
    return f"<style id=\"xephyguard\">{sel}{{display:none!important}}</style>" if sel else ""


def inject_css(html: str, selectors: list[str]) -> str | None:
    css = hide_css(selectors)
    if not css:
        return None
    idx = html.lower().find("</head>")
    if idx < 0:
        return None
    return html[:idx] + css + html[idx:]


BLOCK_PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>Blocked by XephyGuard</title>
<style>body{{font-family:system-ui,sans-serif;background:#f5f5f5;color:#222;display:flex;align-items:center;
justify-content:center;height:100vh;margin:0}}main{{background:#fff;padding:2rem 2.5rem;border-radius:12px;
max-width:520px;box-shadow:0 2px 12px rgba(0,0,0,.08)}}h1{{font-size:1.3rem;margin-top:0}}code{{background:#eee;
padding:2px 6px;border-radius:4px}}</style></head><body><main><h1>This site is not available right now</h1>
<p><code>{host}</code> is blocked on lab computers during the current session ({reason}).</p>
<p>The attempt was recorded. If you think this is a mistake, ask your instructor.</p>
<p style="color:#777;font-size:.85rem">XephyGuard &middot; lab AI-use policy</p></main></body></html>"""


def block_page(host: str, reason: str) -> str:
    esc = lambda s: (s or "").replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")  # noqa: E731
    return BLOCK_PAGE.format(host=esc(host), reason=esc(reason))


class EventShipper:
    """Batches network events and POSTs them to the CCC; falls back to a JSONL file."""

    def __init__(self, cache: PolicyCache, fallback: str = "/var/log/xephyguard/proxy-events.jsonl"):
        self.cache = cache
        self.fallback = fallback
        self.q: queue.Queue[dict[str, Any]] = queue.Queue(maxsize=50000)
        self._stop = threading.Event()
        self._t = threading.Thread(target=self._run, daemon=True)
        self._t.start()

    def emit(self, **ev: Any) -> None:
        ev.setdefault("ts", int(time.time() * 1000))
        try:
            self.q.put_nowait(ev)
        except queue.Full:
            pass

    def stop(self) -> None:
        self._stop.set()
        self._t.join(timeout=3)

    def _run(self) -> None:
        while not self._stop.is_set():
            batch: list[dict[str, Any]] = []
            try:
                batch.append(self.q.get(timeout=1.0))
                while len(batch) < 500:
                    batch.append(self.q.get_nowait())
            except queue.Empty:
                pass
            if batch:
                self._ship(batch)

    def _ship(self, batch: list[dict[str, Any]]) -> None:
        url, token = self.cache.state.events_url, self.cache.state.events_token
        if url:
            try:
                req = urllib.request.Request(url, data=json.dumps({"events": batch}).encode(), method="POST",
                                             headers={"Content-Type": "application/json",
                                                      "Authorization": f"Bearer {token}"})
                with urllib.request.urlopen(req, timeout=5) as r:
                    if 200 <= r.status < 300:
                        return
            except Exception as e:  # noqa: BLE001 - network errors must never break the proxy
                log.warning("event shipping failed (%s); writing to %s", e, self.fallback)
        try:
            os.makedirs(os.path.dirname(self.fallback), exist_ok=True)
            with open(self.fallback, "a") as f:
                for ev in batch:
                    f.write(json.dumps(ev) + "\n")
        except OSError:
            pass
