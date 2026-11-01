"""Exercises the mitmproxy addon with mitmproxy's own test helpers (recorded-style flows)."""
import json

import pytest

mitmproxy = pytest.importorskip("mitmproxy")
from mitmproxy.test import taddons, tflow, tutils  # noqa: E402

from xg_netpolicy import generate  # noqa: E402


@pytest.fixture
def addon(state_file, tmp_path):
    import importlib.util
    import pathlib

    generate.generate(state_file, tmp_path / "net")
    spec = importlib.util.spec_from_file_location(
        "xephyguard_addon", pathlib.Path(__file__).resolve().parent.parent / "addons" / "xephyguard_addon.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    a = mod.XephyGuard()
    with taddons.context(a) as tctx:
        tctx.configure(a, xg_policy=str(tmp_path / "net" / "proxy.json"),
                       xg_events_fallback=str(tmp_path / "events.jsonl"))
        yield a
    a.done()


def _flow(ip, host, path, scheme="https"):
    f = tflow.tflow()
    f.client_conn.peername = (ip, 50000)
    f.request.host = host
    f.request.headers["Host"] = host
    f.request.scheme = scheme
    f.request.port = 443 if scheme == "https" else 80
    f.request.path = path
    return f


def test_google_search_forced_to_web_results(addon):
    f = _flow("192.168.10.21", "www.google.com", "/search?q=quicksort+in+python")
    addon.request(f)
    assert f.request.query.get("udm") == "14"
    assert f.response is None


def test_google_not_rewritten_without_session(addon):
    f = _flow("192.168.10.99", "www.google.com", "/search?q=quicksort")
    addon.request(f)
    assert "udm" not in f.request.query


def test_plain_http_blocked_domain(addon):
    f = _flow("192.168.10.21", "chat.openai.com", "/", scheme="http")
    addon.request(f)
    assert f.response.status_code == 403
    assert b"blocked" in f.response.content


def test_path_rule_blocked(addon):
    f = _flow("192.168.10.21", "huggingface.co", "/chat/")
    addon.request(f)
    assert f.response.status_code == 403


def test_tls_clienthello_blocks_without_decryption(addon):
    from mitmproxy import tls
    from mitmproxy.proxy import context

    client = tutils.tclient_conn() if hasattr(tutils, "tclient_conn") else tflow.tclient_conn()
    client.peername = ("192.168.10.21", 40000)
    ctx = context.Context(client, None)
    ch = tls.ClientHello(_client_hello_bytes("claude.ai"))
    data = tls.ClientHelloData(ctx, ch)
    addon.tls_clienthello(data)
    assert data.ignore_connection is True
    assert client.id in addon.blocked_conns


def test_response_css_injection(addon):
    from mitmproxy import http

    f = _flow("192.168.10.21", "www.google.com", "/search?q=x&udm=14")
    f.response = http.Response.make(200, b"<html><head></head><body>r</body></html>", {"content-type": "text/html"})
    addon.response(f)
    assert b'id="xephyguard"' in f.response.content


def _client_hello_bytes(sni: str) -> bytes:
    """Builds a minimal TLS 1.2 ClientHello handshake body with an SNI extension."""
    import struct

    name = sni.encode()
    sni_ext_data = struct.pack("!H", len(name) + 3) + b"\x00" + struct.pack("!H", len(name)) + name
    ext = struct.pack("!HH", 0, len(sni_ext_data)) + sni_ext_data
    body = (b"\x03\x03" + b"\x00" * 32 + b"\x00" + struct.pack("!H", 2) + b"\x13\x01" + b"\x01\x00"
            + struct.pack("!H", len(ext)) + ext)
    return body
