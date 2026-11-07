#!/usr/bin/env python3
"""Paper metrics from the CCC database (via the analysis package).

    metrics.py detection --db mysql://... --labels labels.csv [--threshold 30]
        labels.csv: session_id,user,label   (label = 1 used AI / 0 honest; from the study protocol)
        -> sensitivity, specificity, FP rate on honest sessions, ROC-AUC, per-method results
    metrics.py overhead --db mysql://... [--since 2026-11-01]
        -> agent CPU / RSS / uplink from heartbeats vs. the acceptance targets
    metrics.py table --out eval/results  -> results_table.md / .tex combining the JSON outputs
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys

import pandas as pd

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "analysis"))
from xg_analysis.db import Db, jload  # noqa: E402

TARGETS = {"cpu_pct": 1.0, "rss_mb": 30.0, "uplink_mb_per_hour": 5.0}


def detection(db: Db, labels: pd.DataFrame, threshold: int) -> dict:
    scores = db.df("SELECT lab_session_id AS session_id, `user`, method, score FROM scores")
    out = {}
    for method, s in scores.groupby("method"):
        m = labels.merge(s, on=["session_id", "user"], how="left").fillna({"score": 0})
        pos, neg = m[m.label == 1], m[m.label == 0]
        tp, fn = int((pos.score >= threshold).sum()), int((pos.score < threshold).sum())
        tn, fp = int((neg.score < threshold).sum()), int((neg.score >= threshold).sum())
        auc = None
        if len(pos) and len(neg):
            from sklearn.metrics import roc_auc_score
            auc = round(float(roc_auc_score(m.label, m.score)), 3)
        out[method] = {"n_pos": len(pos), "n_neg": len(neg), "threshold": threshold, "tp": tp, "fn": fn, "tn": tn, "fp": fp,
                       "sensitivity": round(tp / len(pos), 3) if len(pos) else None,
                       "specificity": round(tn / len(neg), 3) if len(neg) else None,
                       "false_positive_rate": round(fp / len(neg), 3) if len(neg) else None, "roc_auc": auc}
    return out


def overhead(db: Db, since: str | None) -> dict:
    pcs = db.df("SELECT id, name, `usage` FROM pcs")
    rows = []
    for _, p in pcs.iterrows():
        u = jload(p.usage)
        if u:
            rows.append({"pc": p["name"], "cpu_pct": u.get("cpu_pct"), "rss_mb": (u.get("rss_kb") or 0) / 1024})
    df = pd.DataFrame(rows)
    # Uplink: bytes reported in heartbeats is cumulative per agent run; use event volume as a proxy per hour.
    ev = db.df("SELECT pc_id, occurred_at, LENGTH(data) AS bytes FROM events" + (" WHERE occurred_at >= ?" if since else ""),
               (since,) if since else ())
    up = None
    if not ev.empty:
        ev["occurred_at"] = pd.to_datetime(ev.occurred_at, format="mixed")
        hours = max(1 / 60, (ev.occurred_at.max() - ev.occurred_at.min()).total_seconds() / 3600)
        per_pc = ev.groupby("pc_id").bytes.sum() / hours / 1e6 * 1.6   # JSON envelope + HTTP/TLS overhead factor
        up = round(float(per_pc.median()), 3)
    res = {"pcs": len(df), "cpu_pct_median": None if df.empty else round(float(df.cpu_pct.median()), 3),
           "cpu_pct_max": None if df.empty else round(float(df.cpu_pct.max()), 3),
           "rss_mb_median": None if df.empty else round(float(df.rss_mb.median()), 1),
           "rss_mb_max": None if df.empty else round(float(df.rss_mb.max()), 1),
           "uplink_mb_per_hour_median": up, "targets": TARGETS}
    res["meets_targets"] = bool(not df.empty and res["cpu_pct_max"] < TARGETS["cpu_pct"] and res["rss_mb_max"] < TARGETS["rss_mb"]
                                and (up is None or up < TARGETS["uplink_mb_per_hour"]))
    return res


def table(out: pathlib.Path) -> str:
    parts = ["# XephyGuard evaluation results", ""]
    for name in ("summary.json", "detection.json", "overhead.json", "load.json"):
        p = out / name
        if p.exists():
            parts += [f"## {name[:-5]}", "", "```json", p.read_text().strip(), "```", ""]
    if (out / "bypass_table.md").exists():
        parts += ["## Bypass table", "", (out / "bypass_table.md").read_text()]
    md = "\n".join(parts)
    (out / "results_table.md").write_text(md)
    return md


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("detection")
    d.add_argument("--db")
    d.add_argument("--labels", type=pathlib.Path, required=True)
    d.add_argument("--threshold", type=int, default=30)
    d.add_argument("--out", type=pathlib.Path)
    o = sub.add_parser("overhead")
    o.add_argument("--db")
    o.add_argument("--since")
    o.add_argument("--out", type=pathlib.Path)
    t = sub.add_parser("table")
    t.add_argument("--out", type=pathlib.Path, required=True)
    a = ap.parse_args()
    if a.cmd == "table":
        print(table(a.out))
        return 0
    db = Db(a.db)
    res = detection(db, pd.read_csv(a.labels), a.threshold) if a.cmd == "detection" else overhead(db, a.since)
    print(json.dumps(res, indent=1))
    if a.out:
        a.out.mkdir(parents=True, exist_ok=True)
        (a.out / f"{a.cmd}.json").write_text(json.dumps(res, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
