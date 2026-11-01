import json

import pytest

from xg_netpolicy import generate
from xg_netpolicy.domains import DomainSet, PathRules, normalize_domain, parse_list
from xg_netpolicy.proxy_logic import PolicyCache, google_search_rewrite, inject_css
from xg_netpolicy.state import State


def test_normalize_and_parse():
    assert normalize_domain("https://Chat.OpenAI.com/c/123") == "chat.openai.com"
    assert normalize_domain("*.claude.ai") == "claude.ai"
    assert normalize_domain("8.8.8.8") is None
    assert normalize_domain("# comment") is None
    doms, paths = parse_list("openai.com [chat]\n0.0.0.0 bad.example\n# x\nhuggingface.co/chat [chat]\n")
    assert doms == ["bad.example", "openai.com"]
    assert paths == ["huggingface.co/chat"]


def test_domain_suffix_matching():
    ds = DomainSet.of(["openai.com", "x.ai"])
    assert ds.matches("api.openai.com") == "openai.com"
    assert ds.matches("OPENAI.COM.") == "openai.com"
    assert ds.matches("notopenai.com") is None
    assert ds.matches("grok.x.ai") == "x.ai"
    assert ds.matches("max.ai") is None


def test_path_rules():
    pr = PathRules.of(["huggingface.co/chat"])
    assert pr.matches("huggingface.co", "/chat/conversation/1")
    assert pr.matches("huggingface.co", "/chat?x=1")
    assert not pr.matches("huggingface.co", "/models")
    assert not pr.matches("huggingface.co", "/chatterbox")


def test_state_groups(state_dict):
    s = State.from_dict(state_dict)
    assert s.group_of("192.168.10.21") == "test"
    assert s.group_of("192.168.10.22") == "exempt"            # override wins
    assert s.group_of("::ffff:192.168.10.30") == "block_ai"
    assert s.group_of("192.168.10.99") is None
    assert s.blocked_domains_for_group("test").matches("api.githubcopilot.com")
    assert not s.blocked_domains_for_group("block_ai").matches("githubcopilot.com")
    assert s.blocked_domains_for_group(None).matches("dns.google")  # DoH always blocked
    assert not s.blocked_domains_for_group("exempt").matches("openai.com")


def test_state_rejects_foreign_ip(state_dict):
    state_dict["groups"]["test"].append("10.0.0.5")
    with pytest.raises(ValueError):
        State.from_dict(state_dict)


def test_generate_outputs(state_file, tmp_path):
    out = tmp_path / "net"
    changed = generate.generate(state_file, out)
    assert all(changed.values())
    unbound = (out / "unbound-xephyguard.conf").read_text()
    assert 'local-zone: "dns.google." always_nxdomain' in unbound
    assert "access-control-view: 192.168.10.21/32 xg_test" in unbound
    assert 'name: "xg_test"' in unbound
    test_view = unbound.split('name: "xg_test"')[1].split("view:")[0]
    assert 'local-zone: "openai.com." always_nxdomain' in test_view
    assert 'local-zone: "githubcopilot.com." always_nxdomain' in test_view
    block_ai_view = unbound.split('name: "xg_block_ai"')[1].split("view:")[0]
    assert "githubcopilot.com" not in block_ai_view
    nft = (out / "sets.nft").read_text()
    assert "add element inet xephyguard test_clients { 192.168.10.21, 192.168.10.22 }" in nft
    assert "resolver_ips6 { 2001:4860:4860::8888 }" in nft
    proxy = json.loads((out / "proxy.json").read_text())
    assert proxy["version"] == 5
    # unchanged input -> nothing rewritten
    assert not any(generate.generate(state_file, out).values())


def test_tls_decisions(state_file, tmp_path):
    generate.generate(state_file, tmp_path / "net")
    c = PolicyCache(tmp_path / "net" / "proxy.json")
    assert c.decide_tls("192.168.10.21", "chatgpt.com").action == "block"
    assert c.decide_tls("192.168.10.21", "chatgpt.com").category == "ai"
    assert c.decide_tls("192.168.10.21", "www.google.com").action == "inspect"
    assert c.decide_tls("192.168.10.21", "github.com").action == "pass"
    assert c.decide_tls("192.168.10.21", None).action == "block"        # no SNI in Test mode
    assert c.decide_tls("192.168.10.99", "chatgpt.com").action == "pass"  # no session for this PC
    assert c.decide_tls("192.168.10.99", "dns.google").action == "block"  # DoH always blocked
    assert c.decide_tls("192.168.10.22", "chatgpt.com").action == "pass"  # exempt PC
    assert c.decide_tls("192.168.10.40", "example.com").action == "block"  # isolated
    assert c.decide_http("192.168.10.21", "huggingface.co", "/chat/x").action == "block"
    assert c.decide_http("192.168.10.21", "huggingface.co", "/datasets").action == "pass"


def test_google_rewrite():
    q = google_search_rewrite("www.google.com", "/search", [("q", "binary search in c")])
    assert q == [("q", "binary search in c"), ("udm", "14")]
    assert google_search_rewrite("www.google.co.uk", "/search", [("q", "x")])[-1] == ("udm", "14")
    assert google_search_rewrite("www.google.com", "/search", [("q", "x"), ("udm", "14")]) is None
    assert google_search_rewrite("www.google.com", "/search", [("q", "x"), ("udm", "50")])[-1] == ("udm", "14")
    assert google_search_rewrite("www.google.com", "/search", [("q", "x"), ("tbm", "isch")]) is None
    assert google_search_rewrite("www.google.com", "/maps", [("q", "x")]) is None
    assert google_search_rewrite("evilgoogle.com", "/search", [("q", "x")]) is None


def test_css_injection():
    html = "<html><head><title>x</title></head><body></body></html>"
    out = inject_css(html, ["#ai", "div[data-x]"])
    assert '<style id="xephyguard">#ai,div[data-x]{display:none!important}</style></head>' in out
    assert inject_css(html, ["</style><script>"]) is None
