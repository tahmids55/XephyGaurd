"""Characterization tests: pin CURRENT behavior (docs/features/eval). Not a spec."""
import argparse
import json
import pathlib
import sys

import pandas as pd

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "export"))
sys.path.insert(0, str(ROOT / "simulator"))
sys.path.insert(0, str(ROOT.parent / "analysis"))
import fake_agents  # noqa: E402
import metrics  # noqa: E402
from xg_analysis.db import Db  # noqa: E402

SCHEMA = (ROOT.parent / "analysis" / "tests" / "schema.sql").read_text()


def make_db():
    d = Db("sqlite:///:memory:")
    d.conn.executescript(SCHEMA)
    return d


# ------------------------------------------------------------------ F120 simulator
def test_f120_mac_addresses_are_locally_administered_and_unique():
    assert fake_agents.mac(1) == "02:58:47:00:00:01"
    assert fake_agents.mac(0x010203) == "02:58:47:01:02:03"
    assert len({fake_agents.mac(i) for i in range(500)}) == 500


def test_f120_virtual_pc_events_are_sequenced_and_loop_posts_valid_batches(tmp_path, monkeypatch):
    a = argparse.Namespace(ca="x", insecure_hostname=False, ccc="h", port=8443, key="k")
    pc = fake_agents.VirtualPc(3, a, tmp_path)
    pc.pc_id, pc.token = "9", "t"
    e1, e2 = pc.event("paste", {}), pc.event("paste", {})
    assert (e1["seq"], e2["seq"]) == (1, 2)
    posted = []

    def fake_req(method, path, body=None, client=True):
        posted.append((method, path, body))
        return 200, {}

    t = {"now": 1000.0}
    monkeypatch.setattr(pc, "req", fake_req)
    monkeypatch.setattr(fake_agents.time, "time", lambda: t["now"])
    monkeypatch.setattr(fake_agents.time, "sleep", lambda s: t.__setitem__("now", t["now"] + 10))
    pc.loop(until=1015.0)
    assert len(posted) == 2 and len(pc.latencies) == 2 and pc.errors == 0
    method, path, body = posted[0]
    assert (method, path) == ("POST", "/api/agent/v1/events")
    assert body["v"] == 1 and body["pc_id"] == "9"
    assert body["events"][0]["type"] == "key_stats" and body["events"][0]["data"]["user"] == "sim003"
    assert body["heartbeat"]["mode"] == "test"
    seqs = [e["seq"] for _, _, b in posted for e in b["events"]]
    assert seqs == sorted(seqs) and len(set(seqs)) == len(seqs)


def test_f120_non_200_and_network_errors_are_counted(tmp_path, monkeypatch):
    a = argparse.Namespace(ca="x", insecure_hostname=False, ccc="h", port=8443, key="k")
    pc = fake_agents.VirtualPc(1, a, tmp_path)
    answers = iter([(500, {}), OSError("down")])

    def fake_req(*args, **kw):
        r = next(answers)
        if isinstance(r, Exception):
            raise r
        return r

    t = {"now": 0.0}
    monkeypatch.setattr(pc, "req", fake_req)
    monkeypatch.setattr(fake_agents.time, "time", lambda: t["now"])
    monkeypatch.setattr(fake_agents.time, "sleep", lambda s: t.__setitem__("now", t["now"] + 10))
    pc.loop(until=15.0)
    assert pc.errors == 2 and pc.latencies == []


# ------------------------------------------------------------------ F121 metrics
def _scores(db, rows):
    db.conn.execute("INSERT INTO labs (id, name) VALUES (1,'L')")
    db.conn.execute("INSERT INTO profiles (id,name,mode,network,agent) VALUES (1,'T','test','{}','{}')")
    db.conn.execute("INSERT INTO lab_sessions (id,lab_id,profile_id,course,mode,starts_at) VALUES (1,1,1,'C','test','2026-01-01')")
    db.conn.execute("INSERT INTO pcs (id,lab_id,name,hostname) VALUES (1,1,'P','p')")
    for u, m, s in rows:
        db.conn.execute("INSERT INTO scores (lab_session_id,pc_id,user,method,score,reasons) VALUES (1,1,?,?,?,'[]')", (u, m, s))
    db.conn.commit()


def test_f121_detection_metrics_counts_threshold_and_rounding():
    db = make_db()
    _scores(db, [("a", "v1", 80), ("b", "v1", 10), ("c", "v1", 40), ("d", "v1", 5)])
    labels = pd.DataFrame({"session_id": [1, 1, 1, 1, 1], "user": ["a", "b", "c", "d", "ghost"], "label": [1, 1, 0, 0, 1]})
    r = metrics.detection(db, labels, 30)["v1"]
    # ghost has no score row -> treated as score 0 (a miss)
    assert (r["tp"], r["fn"], r["tn"], r["fp"]) == (1, 2, 1, 1)
    assert r["sensitivity"] == 0.333 and r["specificity"] == 0.5 and r["false_positive_rate"] == 0.5
    assert r["n_pos"] == 3 and r["n_neg"] == 2 and r["threshold"] == 30
    assert r["roc_auc"] is not None
    assert metrics.detection(db, labels, 90)["v1"]["tp"] == 0


def test_f121_detection_without_negatives_gives_none_rates_and_per_method_keys():
    db = make_db()
    _scores(db, [("a", "v1", 80), ("a", "v2", 20)])
    labels = pd.DataFrame({"session_id": [1], "user": ["a"], "label": [1]})
    out = metrics.detection(db, labels, 30)
    assert set(out) == {"v1", "v2"}
    assert out["v1"]["specificity"] is None and out["v1"]["roc_auc"] is None
    assert out["v1"]["sensitivity"] == 1.0 and out["v2"]["sensitivity"] == 0.0


def test_f121_overhead_targets_and_empty_case():
    db = make_db()
    assert metrics.overhead(db, None)["meets_targets"] is False and metrics.overhead(db, None)["pcs"] == 0
    db.conn.execute("INSERT INTO labs (id, name) VALUES (1,'L')")
    db.conn.execute("INSERT INTO pcs (id,lab_id,name,hostname,usage) VALUES (1,1,'P','p', ?)", (json.dumps({"cpu_pct": 0.4, "rss_kb": 20480}),))
    db.conn.execute("INSERT INTO pcs (id,lab_id,name,hostname,usage) VALUES (2,1,'Q','q', ?)", (json.dumps({"cpu_pct": 2.0, "rss_kb": 10240}),))
    db.conn.commit()
    r = metrics.overhead(db, None)
    assert (r["pcs"], r["cpu_pct_max"], r["rss_mb_max"], r["rss_mb_median"]) == (2, 2.0, 20.0, 15.0)
    assert r["meets_targets"] is False                                  # cpu 2.0 >= 1.0
    assert r["targets"] == {"cpu_pct": 1.0, "rss_mb": 30.0, "uplink_mb_per_hour": 5.0}
    db.conn.execute("UPDATE pcs SET usage = ? WHERE id = 2", (json.dumps({"cpu_pct": 0.5, "rss_kb": 10240}),))
    db.conn.commit()
    assert metrics.overhead(db, None)["meets_targets"] is True


def test_f121_table_combines_present_json_files_only(tmp_path):
    (tmp_path / "detection.json").write_text('{"v1": {}}')
    (tmp_path / "bypass_table.md").write_text("| a |")
    md = metrics.table(tmp_path)
    assert md.startswith("# XephyGuard evaluation results")
    assert "## detection" in md and "## overhead" not in md and "## Bypass table" in md
    assert (tmp_path / "results_table.md").read_text() == md
