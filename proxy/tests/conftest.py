import json
import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))


@pytest.fixture
def state_dict():
    return {
        "version": 5,
        "ccc_ip": "192.168.10.2",
        "lab_subnet": "192.168.10.0/24",
        "lan_if": "enp2s0",
        "wan_if": "enp1s0",
        "groups": {"test": ["192.168.10.21", "192.168.10.22"], "block_ai": ["192.168.10.30"],
                   "isolated": ["192.168.10.40"], "exempt": ["192.168.10.22"]},
        "categories": {"ai": ["openai.com", "chatgpt.com", "claude.ai"], "ide": ["githubcopilot.com"],
                       "marketplace": ["marketplace.visualstudio.com"], "vpn": ["protonvpn.com"],
                       "doh": ["dns.google", "cloudflare-dns.com"]},
        "path_rules": {"ai": ["huggingface.co/chat", "duckduckgo.com/aichat"]},
        "group_categories": {"test": ["ai", "ide", "marketplace", "vpn"], "block_ai": ["ai"]},
        "always_block": ["doh"],
        "doh_ips": ["8.8.8.8", "1.1.1.1", "2001:4860:4860::8888"],
        "google": {"force_udm14": True, "hide_selectors": ["#xg-ai-overview"]},
        "inspect_hosts": ["google.com", "www.bing.com", "huggingface.co", "duckduckgo.com"],
        "log_urls": {"enabled": False},
    }


@pytest.fixture
def state_file(tmp_path, state_dict):
    p = tmp_path / "state.json"
    p.write_text(json.dumps(state_dict))
    return p
