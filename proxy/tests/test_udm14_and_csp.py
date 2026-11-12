"""Unit tests for per-PC udm14_clients, search rewriting, /async/folwr blocking, and CSP-safe CSS injection."""
import json
import pytest

from xg_netpolicy import state as netstate
from xg_netpolicy.proxy_logic import (
    PolicyCache,
    extract_nonce,
    google_search_rewrite,
    hide_css,
    inject_css,
    relax_style_csp,
)


def test_state_validates_udm14_clients(state_dict):
    state_dict["google"]["udm14_clients"] = ["192.168.10.21", "192.168.10.22"]
    netstate.State.from_dict(state_dict).validate()

    bad = dict(state_dict)
    bad["google"] = dict(state_dict["google"], udm14_clients=["10.99.0.5"])
    with pytest.raises(ValueError, match="udm14_clients"):
        netstate.State.from_dict(bad).validate()


def test_always_on_udm14_client_inspects_google_only(tmp_path, state_dict):
    # Empty test/block_ai groups (always_on), but 192.168.10.21 wants udm14 suppression
    state_dict["groups"] = {"test": [], "block_ai": [], "isolated": [], "exempt": ["192.168.10.25"]}
    state_dict["inspect_hosts"] = ["google.com", "www.google.com", "google.co.uk", "huggingface.co"]
    state_dict["google"]["force_udm14"] = True
    state_dict["google"]["udm14_clients"] = ["192.168.10.21", "192.168.10.25"]
    p = tmp_path / "state.json"
    p.write_text(json.dumps(state_dict))

    c = PolicyCache(p)
    assert c.wants_google_rewrite("192.168.10.21") is True
    assert c.wants_google_rewrite("192.168.10.99") is False
    assert c.wants_google_rewrite("192.168.10.25") is False  # exempt wins

    assert c.decide_tls("192.168.10.21", "www.google.com").action == "inspect"
    assert c.decide_tls("192.168.10.21", "google.co.uk").action == "inspect"
    assert c.decide_tls("192.168.10.21", "huggingface.co").action == "pass"
    assert c.decide_tls("192.168.10.21", "github.com").action == "pass"
    assert c.decide_tls("192.168.10.99", "www.google.com").action == "pass"
    assert c.decide_tls("192.168.10.25", "www.google.com").action == "pass"


def test_google_search_rewrite_sets_udm14_replaces_udm50_and_strips_aep():
    q = google_search_rewrite("www.google.com", "/search", [("q", "binary search"), ("aep", "1")])
    assert q is not None
    assert dict(q) == {"q": "binary search", "udm": "14"}

    q2 = google_search_rewrite("google.co.uk", "/search", [("q", "dp"), ("udm", "50")])
    assert q2 is not None and dict(q2) == {"q": "dp", "udm": "14"}

    # Already udm=14 without aep -> unchanged (None)
    assert google_search_rewrite("www.google.com", "/search", [("q", "dp"), ("udm", "14")]) is None
    # Non-AI verticals like images (udm=2) stay untouched
    assert google_search_rewrite("www.google.com", "/search", [("q", "cat"), ("udm", "2")]) is None


def test_csp_nonce_and_style_relaxation():
    html = '<!doctype html><html><head><script nonce="n0nc3==">1</script></head><body><div id="app">hi</div></body></html>'
    nonce = extract_nonce(html)
    assert nonce == "n0nc3=="

    selectors = ['div[jsname="txosbe"]', "#xg-ai-overview"]
    out = inject_css(html, selectors, nonce=nonce)
    assert out is not None
    assert 'nonce="n0nc3=="' in out
    assert hide_css(selectors, nonce=nonce).startswith('<style id="xephyguard" nonce="n0nc3==">')

    csp = "default-src 'self'; script-src 'nonce-abc'; style-src 'self' 'nonce-xyz'"
    relaxed = relax_style_csp(csp)
    assert "'unsafe-inline'" in relaxed
    assert "'nonce-xyz'" not in relaxed


def test_addon_blocks_folwr_and_relaxes_csp(tmp_path, state_dict):
    pytest.importorskip("mitmproxy")
    from mitmproxy.test import tflow
    import pathlib
    import sys

    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "addons"))
    import xephyguard_addon as mod

    state_dict["groups"] = {"test": [], "block_ai": [], "isolated": [], "exempt": []}
    state_dict["google"]["force_udm14"] = True
    state_dict["google"]["udm14_clients"] = ["192.168.10.21"]
    p = tmp_path / "state.json"
    p.write_text(json.dumps(state_dict))

    addon = mod.XephyGuard()
    addon.cache = PolicyCache(p)

    # /async/folwr should be blocked with 204
    flow = tflow.tflow()
    flow.client_conn.peername = ("192.168.10.21", 50000)
    flow.request.host = "www.google.com"
    flow.request.port = 443
    flow.request.scheme = "https"
    flow.request.path = "/async/folwr?q=test"
    addon.request(flow)
    assert flow.response is not None
    assert flow.response.status_code == 204
