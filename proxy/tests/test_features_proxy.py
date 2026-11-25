"""Characterization tests: pin CURRENT behavior (docs/features/proxy). Not a spec.
KNOWN BUG I-PROXY-n marks behavior that looks wrong but is asserted as-is."""
import json

import pytest

from xg_netpolicy import generate
from xg_netpolicy.domains import DomainSet, PathRules, normalize_domain, parse_list
from xg_netpolicy.proxy_logic import PolicyCache
from xg_netpolicy.state import State


@pytest.fixture
def cache(state_file, tmp_path):
    generate.generate(state_file, tmp_path / "net")
    return PolicyCache(tmp_path / "net" / "proxy.json")


# ------------------------------------------------------------------ F065 DNS views
def test_f065_isolated_view_refuses_everything_and_other_views_only_nxdomain_their_categories(state_file, tmp_path):
    generate.generate(state_file, tmp_path / "net")
    unbound = (tmp_path / "net" / "unbound-xephyguard.conf").read_text()
    iso = unbound.split('name: "xg_isolated"')[1]
    assert 'local-zone: "." always_refuse' in iso
    assert "access-control-view: 192.168.10.40/32 xg_isolated" in unbound
    # the exempt PC (also in group test) is still mapped to the test view: exemption is NOT applied in DNS
    assert "access-control-view: 192.168.10.22/32 xg_test" in unbound   # KNOWN BUG I-PROXY-1: exempt PC still DNS-blocked
    ai_view = unbound.split('name: "xg_block_ai"')[1].split("view:")[0]
    assert 'local-zone: "claude.ai." always_nxdomain' in ai_view
    assert "marketplace.visualstudio.com" not in ai_view
    assert "view-first: yes" in unbound


def test_f065_empty_groups_produce_no_client_mappings(state_dict, tmp_path):
    state_dict["groups"] = {"test": [], "block_ai": [], "isolated": [], "exempt": []}
    p = tmp_path / "s.json"
    p.write_text(json.dumps(state_dict))
    generate.generate(p, tmp_path / "n")
    unbound = (tmp_path / "n" / "unbound-xephyguard.conf").read_text()
    assert "access-control-view:" not in unbound
    assert 'local-zone: "dns.google." always_nxdomain' in unbound   # DoH blocked for everyone


# ------------------------------------------------------------------ F066 nftables
def test_f066_nft_sets_flush_every_group_and_split_ipv4_ipv6_resolvers(state_file, tmp_path):
    generate.generate(state_file, tmp_path / "net")
    nft = (tmp_path / "net" / "sets.nft").read_text()
    for name in ("test_clients", "block_ai_clients", "isolated_clients", "exempt_clients", "resolver_ips", "resolver_ips6"):
        assert f"flush set inet xephyguard {name}" in nft
    assert "add element inet xephyguard block_ai_clients { 192.168.10.30 }" in nft
    assert "add element inet xephyguard resolver_ips { 8.8.8.8, 1.1.1.1 }" in nft
    assert "add element inet xephyguard exempt_clients { 192.168.10.22 }" in nft


def test_f066_empty_group_is_flushed_without_add_element(state_dict, tmp_path):
    state_dict["groups"]["isolated"] = []
    p = tmp_path / "s.json"
    p.write_text(json.dumps(state_dict))
    generate.generate(p, tmp_path / "n")
    nft = (tmp_path / "n" / "sets.nft").read_text()
    assert "flush set inet xephyguard isolated_clients" in nft
    assert "add element inet xephyguard isolated_clients" not in nft


def test_f066_broken_state_never_replaces_working_output(state_file, tmp_path, state_dict):
    generate.generate(state_file, tmp_path / "net")
    before = (tmp_path / "net" / "sets.nft").read_text()
    state_dict["groups"]["test"].append("10.9.9.9")
    state_file.write_text(json.dumps(state_dict))
    with pytest.raises(ValueError):
        generate.generate(state_file, tmp_path / "net")
    assert (tmp_path / "net" / "sets.nft").read_text() == before


def test_f066_static_ruleset_template_has_the_documented_layers():
    import pathlib
    text = (pathlib.Path(__file__).resolve().parent.parent / "nftables" / "xephyguard.nft").read_text()
    for needle in ("QUIC", "DoT", "resolver_ips", "redirect to :@PROXY_PORT@", "isolated_clients", "policy drop"):
        assert needle in text


# ------------------------------------------------------------------ F067 SNI blocking
def test_f067_sni_decisions_per_group(cache):
    d = cache.decide_tls
    assert d("192.168.10.30", "claude.ai").action == "block"        # block_ai group
    assert d("192.168.10.30", "githubcopilot.com").action == "pass"  # ide not blocked for block_ai
    assert d("192.168.10.30", None).action == "pass"                 # no SNI is only fatal in Test mode
    assert d("192.168.10.21", "API.OpenAI.com.").action == "block"   # case + trailing dot
    assert d("192.168.10.21", "x.marketplace.visualstudio.com").category == "marketplace"
    assert d(None, "chatgpt.com").action == "pass"
    d_iso = d("192.168.10.40", None)
    assert (d_iso.action, d_iso.category) == ("block", "isolated")


def test_f067_unreadable_state_keeps_previous_policy(state_file, tmp_path):
    generate.generate(state_file, tmp_path / "net")
    c = PolicyCache(tmp_path / "net" / "proxy.json", interval=0)
    assert c.decide_tls("192.168.10.21", "chatgpt.com").action == "block"
    (tmp_path / "net" / "proxy.json").write_text("{not json")
    c.reload(force=True)
    assert c.decide_tls("192.168.10.21", "chatgpt.com").action == "block"


# ------------------------------------------------------------------ F068 path rules
def test_f068_path_rules_block_path_but_not_the_rest_of_the_host(cache):
    h = cache.decide_http
    r = h("192.168.10.21", "huggingface.co", "/chat/abc")
    assert (r.action, r.category, r.reason) == ("block", "ai", "path rule")
    assert h("192.168.10.21", "huggingface.co", "/models/x").action == "pass"
    assert h("192.168.10.21", "duckduckgo.com", "/aichat").action == "block"
    assert h("192.168.10.21", "duckduckgo.com", "/?q=x").action == "pass"
    assert h("192.168.10.99", "huggingface.co", "/chat").action == "pass"   # PC not in a session
    assert h("192.168.10.40", "example.com", "/").reason == "PC is isolated"


def test_f068_inspection_only_for_listed_hosts_in_test_and_block_ai(cache):
    assert cache.decide_tls("192.168.10.21", "huggingface.co").action == "inspect"
    assert cache.decide_tls("192.168.10.30", "www.bing.com").action == "inspect"
    assert cache.decide_tls("192.168.10.21", "example.org").action == "pass"
    assert cache.decide_tls("192.168.10.99", "huggingface.co").action == "pass"


def test_f068_domain_helpers_edge_cases():
    assert normalize_domain("") is None
    assert normalize_domain("localhost") is None   # single-label names are not domains
    doms, paths = parse_list("")
    assert (doms, paths) == ([], [])
    assert not PathRules.of([]).matches("a.com", "/x")
    assert DomainSet.of([]).matches("a.com") is None
