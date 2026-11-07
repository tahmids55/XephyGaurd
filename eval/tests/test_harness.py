import pathlib
import sys

import yaml

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "harness"))
import redteam  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent


def test_scenario_files_are_valid():
    for f in (ROOT / "scenarios").glob("*.yaml"):
        doc = yaml.safe_load(f.read_text())
        ids = [s["id"] for s in doc["scenarios"]]
        assert len(ids) == len(set(ids)), f
        for s in doc["scenarios"]:
            assert s["layer"] and s["title"] and s.get("expect"), s["id"]
            assert s.get("run") or s.get("manual"), s["id"]


def test_spec_scenarios_are_covered():
    ids = {s["id"] for s in yaml.safe_load((ROOT / "scenarios" / "linux.yaml").read_text())["scenarios"]}
    for required in ["web.chatgpt", "search.ai_overview", "web.copilot", "bypass.proxy_env", "bypass.vpn_tun", "bypass.tor",
                     "bypass.doh", "bypass.quic", "local.ollama", "local.llamacpp", "local.container", "ide.copilot_ext",
                     "cli.claude_code", "usb.portable_editor", "behavior.large_paste", "tamper.stop_agent", "tamper.clock",
                     "bypass.hosts_file", "behavior.phone_hotspot"]:
        assert required in ids


def test_blocked_checks():
    assert redteam.blocked_check({"stdout_not_in": ["200"]}, "000")
    assert not redteam.blocked_check({"stdout_not_in": ["200"]}, "200")
    assert redteam.blocked_check({"stdout_contains": "denied"}, "sudo: a password is required\ndenied")
    assert redteam.blocked_check({"stdout_empty": True}, "  ")


def test_evidence_matching_and_tables(tmp_path):
    res = {"alerts": [{"rule_id": "proc.ollama", "created_at": "2026-11-02T09:00:03+00:00"}],
           "net_events": [{"host": "chat.openai.com", "action": "blocked", "layer": "sni", "occurred_at": "2026-11-02T09:00:01+00:00"}]}
    assert redteam.evidence_match({"alert_rule": "proc.ollama"}, res)[0]
    assert redteam.evidence_match({"net_blocked_host": "openai.com"}, res)[0]
    assert not redteam.evidence_match({"alert_rule": "proc.lmstudio"}, res)[0]
    rows = [redteam.Result("a", "network", "A", "blocked", True, False, None, "", "", 0),
            redteam.Result("b", "agent", "B", "detected", False, True, 2.5, "alert", "", 0),
            redteam.Result("c", "agent", "C", "missed", False, False, None, "", "", 0)]
    redteam.write(tmp_path, rows)
    assert [r.outcome for r in redteam.load(tmp_path)] == ["blocked", "detected", "missed"]
    md = redteam.table_markdown(rows)
    assert "| agent | 2 | 0 | 1 | 1 | 50% |" in md
    assert "| **overall** | 3 | 1 | 1 | 1 | 33% |" in md
    assert "\\toprule" in redteam.table_latex(rows)


def test_pilot_governance_summary_and_fairness():
    sys.path.insert(0, str(ROOT / "export"))
    sys.path.insert(0, str(ROOT.parent / "analysis"))
    import metrics  # noqa: E402
    from xg_analysis.fairness import audit_fairness  # noqa: E402

    records = [
        {"student_id": "s1", "group": "native_typist", "ground_truth_honest": True, "score": 10.0, "exempt": False},
        {"student_id": "s2", "group": "non_native_typist", "ground_truth_honest": True, "score": 15.0, "exempt": False},
        {"student_id": "s3", "group": "accommodated", "ground_truth_honest": True, "score": 90.0, "exempt": True},
        {"student_id": "s4", "group": "native_typist", "ground_truth_honest": False, "score": 88.0, "rule_hits": ["proc.ollama"]},
    ]
    fairness = audit_fairness(records, score_threshold=50.0)
    appeals = [
        {"id": 1, "status": "overturned"},
        {"id": 2, "status": "upheld"},
        {"id": 3, "status": "upheld"},
        {"id": 4, "status": "noted"},
        {"id": 5, "status": "open"},
    ]
    gov = metrics.summarize_pilot_governance(appeals, fairness)
    assert gov["appeals_total"] == 5
    assert gov["appeals_open"] == 1
    assert gov["appeals_decided"] == 4
    assert gov["appeals_overturned"] == 1
    assert gov["overturn_rate"] == 0.25
    assert gov["passes_fairness_gate"] is True
    assert gov["max_fpr_gap"] == 0.0
    assert gov["low_precision_rules"] == []
