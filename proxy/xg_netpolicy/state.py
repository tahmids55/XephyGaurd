"""Typed view of the network policy state document written by the CCC."""
from __future__ import annotations

import ipaddress
import json
import pathlib
from dataclasses import dataclass, field
from typing import Any

from .domains import DomainSet, PathRules

GROUPS = ("test", "block_ai", "isolated", "exempt")


@dataclass
class State:
    version: int = 0
    ccc_ip: str = "192.168.10.2"
    lab_subnet: str = "192.168.10.0/24"
    lan_if: str = "lan0"
    wan_if: str = "wan0"
    proxy_port: int = 8080
    agent_port: int = 8443
    network_mode: str = "gateway"
    groups: dict[str, list[str]] = field(default_factory=lambda: {g: [] for g in GROUPS})
    categories: dict[str, list[str]] = field(default_factory=dict)
    path_rules: dict[str, list[str]] = field(default_factory=dict)
    group_categories: dict[str, list[str]] = field(default_factory=dict)
    always_block: list[str] = field(default_factory=lambda: ["doh"])
    doh_ips: list[str] = field(default_factory=list)
    google: dict[str, Any] = field(default_factory=lambda: {"force_udm14": True, "udm14_clients": [], "hide_selectors": [], "strip_markers": []})
    inspect_hosts: list[str] = field(default_factory=list)
    log_urls: dict[str, Any] = field(default_factory=lambda: {"enabled": False, "query": False})
    events_url: str = ""
    events_token: str = ""

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "State":
        s = cls()
        for k, v in d.items():
            if hasattr(s, k):
                setattr(s, k, v)
        for g in GROUPS:
            s.groups.setdefault(g, [])
        s.validate()
        return s

    @classmethod
    def load(cls, path: str | pathlib.Path) -> "State":
        return cls.from_dict(json.loads(pathlib.Path(path).read_text()))

    def validate(self) -> None:
        ipaddress.ip_address(self.ccc_ip)
        net = ipaddress.ip_network(self.lab_subnet, strict=False)
        for g, ips in self.groups.items():
            for ip in ips:
                if ipaddress.ip_address(ip) not in net:
                    raise ValueError(f"{g}: {ip} is outside the lab subnet {self.lab_subnet}")
        for ip in self.google.get("udm14_clients") or []:
            if ipaddress.ip_address(ip) not in net:
                raise ValueError(f"udm14_clients: {ip} is outside the lab subnet {self.lab_subnet}")
        for name in self.lan_if, self.wan_if:
            if not name.replace("-", "").replace("_", "").replace(".", "").isalnum():
                raise ValueError(f"bad interface name {name!r}")

    # -- derived views ------------------------------------------------------------------------
    def group_of(self, ip: str | None) -> str | None:
        """Most restrictive group a client belongs to (exempt wins over everything)."""
        if not ip:
            return None
        ip = ip.removeprefix("::ffff:")
        for g in ("exempt", "isolated", "test", "block_ai"):
            if ip in self.groups.get(g, []):
                return g
        return None

    def blocked_domains_for_group(self, group: str | None) -> DomainSet:
        cats = list(self.always_block)
        if group and group != "exempt":
            cats += self.group_categories.get(group, [])
        doms: list[str] = []
        for c in cats:
            doms += self.categories.get(c, [])
        return DomainSet.of(doms)

    def path_rules_for_group(self, group: str | None) -> PathRules:
        if not group or group == "exempt":
            return PathRules()
        rules: list[str] = []
        for c in self.group_categories.get(group, []):
            rules += self.path_rules.get(c, [])
        return PathRules.of(rules)

    def category_of(self, domain: str) -> str | None:
        for c, doms in self.categories.items():
            if DomainSet.of(doms).matches(domain):
                return c
        return None
