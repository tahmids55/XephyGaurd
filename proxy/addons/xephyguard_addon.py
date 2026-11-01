"""mitmproxy addon implementing XephyGuard's proxy layers.

Run (transparent mode, gateway):
    mitmdump --mode transparent --listen-port 8080 --showhost \
             --set confdir=/var/lib/xephyguard/mitmproxy \
             -s /opt/xephyguard/proxy/addons/xephyguard_addon.py \
             --set xg_policy=/var/lib/xephyguard/net/proxy.json

Behaviour per TLS connection (decided at ClientHello, before any decryption):
  * blocked domain for this client's group  -> connection refused (SNI filter, no decryption)
  * selective-inspection host (e.g. Google search) for a restricted client -> intercepted
  * everything else -> passed through untouched (never decrypted)
"""
from __future__ import annotations

import logging
import os
import sys

from mitmproxy import ctx, http, tls
from mitmproxy.proxy import server_hooks

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from xg_netpolicy.proxy_logic import (  # noqa: E402
    GOOGLE_HOST,
    EventShipper,
    PolicyCache,
    block_page,
    google_search_rewrite,
    inject_css,
)

log = logging.getLogger("xephyguard")


class XephyGuard:
    def __init__(self) -> None:
        self.cache: PolicyCache | None = None
        self.events: EventShipper | None = None
        self.blocked_conns: set[str] = set()

    def load(self, loader) -> None:
        loader.add_option("xg_policy", str, "/var/lib/xephyguard/net/proxy.json", "XephyGuard proxy policy file")
        loader.add_option("xg_events_fallback", str, "/var/log/xephyguard/proxy-events.jsonl", "JSONL fallback")

    def configure(self, updated) -> None:
        if "xg_policy" in updated or self.cache is None:
            self.cache = PolicyCache(ctx.options.xg_policy)
            if self.events is None:
                self.events = EventShipper(self.cache, ctx.options.xg_events_fallback)

    def done(self) -> None:
        if self.events:
            self.events.stop()

    # ---------------------------------------------------------------- TLS (no decryption needed)
    def tls_clienthello(self, data: tls.ClientHelloData) -> None:
        assert self.cache and self.events
        client = data.context.client
        ip = client.peername[0] if client.peername else None
        sni = data.client_hello.sni
        d = self.cache.decide_tls(ip, sni)
        if d.action == "block":
            data.ignore_connection = True          # do not decrypt ...
            self.blocked_conns.add(client.id)      # ... and refuse the upstream connection below
            self.events.emit(client_ip=ip, host=sni or "", layer="sni", action="blocked", category=d.category,
                             matched=d.matched, reason=d.reason)
        elif d.action == "pass":
            data.ignore_connection = True
            if self.cache.state.log_urls.get("enabled") and sni:
                self.events.emit(client_ip=ip, host=sni, layer="sni", action="allowed")
        # "inspect": let mitmproxy intercept with the lab CA

    def server_connect(self, data: server_hooks.ServerConnectionHookData) -> None:
        if data.client.id in self.blocked_conns:
            data.server.error = "blocked by XephyGuard"

    def client_disconnected(self, client) -> None:
        self.blocked_conns.discard(client.id)

    # ------------------------------------------------- HTTP and inspected HTTPS requests/responses
    def request(self, flow: http.HTTPFlow) -> None:
        assert self.cache and self.events
        ip = flow.client_conn.peername[0] if flow.client_conn.peername else None
        host = flow.request.pretty_host
        path = flow.request.path
        d = self.cache.decide_http(ip, host, path)
        if d.action == "block":
            flow.response = http.Response.make(403, block_page(host, d.reason), {"Content-Type": "text/html; charset=utf-8"})
            self.events.emit(client_ip=ip, host=host, path=path.split("?", 1)[0][:256],
                             layer="http" if flow.request.scheme == "http" else "path", action="blocked",
                             category=d.category, matched=d.matched, reason=d.reason)
            return
        if self.cache.wants_google_rewrite(ip):
            new_q = google_search_rewrite(host, path, list(flow.request.query.items(multi=True)))
            if new_q is not None:
                flow.request.query.clear()
                for k, v in new_q:
                    flow.request.query.add(k, v)
                flow.metadata["xg_rewritten"] = True
                self.events.emit(client_ip=ip, host=host, path="/search", layer="search-ai", action="rewritten",
                                 category="search_ai")
        lu = self.cache.state.log_urls
        if lu.get("enabled"):
            p = flow.request.path if lu.get("query") else flow.request.path.split("?", 1)[0]
            self.events.emit(client_ip=ip, host=host, path=p[:512], layer="http", action="allowed")

    def response(self, flow: http.HTTPFlow) -> None:
        assert self.cache
        ip = flow.client_conn.peername[0] if flow.client_conn.peername else None
        if not self.cache.wants_google_rewrite(ip) or not flow.response:
            return
        ctype = flow.response.headers.get("content-type", "")
        if "text/html" not in ctype:
            return
        selectors = self.cache.state.google.get("hide_selectors", [])
        host = flow.request.pretty_host.lower()
        if selectors and (GOOGLE_HOST.match(host) or host.endswith("bing.com")):
            html = flow.response.get_text(strict=False) or ""
            new = inject_css(html, selectors)
            if new:
                flow.response.set_text(new)


addons = [XephyGuard()]
