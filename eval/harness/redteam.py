#!/usr/bin/env python3
"""XephyGuard red-team harness.

Runs on the CCC. For each scenario it executes the attack on a target lab PC over SSH while a Test
session is active, then asks the CCC (internal API on 127.0.0.1:8000) what it observed. Produces
results.csv and a bypass table (markdown + LaTeX) with no manual data entry, except for scenarios
that are manual by nature (phone hotspot), which are recorded with `record`.

    redteam.py run --target student@192.168.10.21 --scenarios eval/scenarios/linux.yaml --out eval/results/run1
    redteam.py record --out eval/results/run1 behavior.phone_hotspot detected
    redteam.py table  --out eval/results/run1
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import pathlib
import shlex
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from dataclasses import asdict, dataclass

import yaml

OUTCOMES = ("blocked", "detected", "missed", "skipped")


@dataclass
class Result:
    id: str
    layer: str
    title: str
    outcome: str
    blocked: bool
    detected: bool
    time_to_detect_s: float | None
    evidence: str
    stdout: str
    started_ms: int


def ssh(target: str, cmd: str, timeout: int) -> tuple[int, str]:
    if target in ("local", "localhost"):
        p = subprocess.run(["bash", "-lc", cmd], capture_output=True, text=True, timeout=timeout)
    else:
        p = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", target, cmd],
                           capture_output=True, text=True, timeout=timeout)
    return p.returncode, (p.stdout + p.stderr).strip()


def ccc_results(api: str, token: str, pc_ip: str, since_ms: int) -> dict:
    q = urllib.parse.urlencode({"pc_ip": pc_ip, "since_ms": since_ms})
    req = urllib.request.Request(f"{api}/api/internal/eval/results?{q}", headers={"Authorization": f"Bearer {token}",
                                                                                   "Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read())


def blocked_check(rule: dict | None, out: str) -> bool:
    if not rule:
        return False
    o = out.strip().splitlines()[-1].strip() if out.strip() else ""
    if "stdout_in" in rule:
        return o in rule["stdout_in"]
    if "stdout_not_in" in rule:
        return o not in rule["stdout_not_in"]
    if "stdout_contains" in rule:
        return rule["stdout_contains"] in out
    if "stdout_not_contains" in rule:
        return rule["stdout_not_contains"] not in out
    if "stdout_empty" in rule:
        return out.strip() == ""
    return False


def evidence_match(expect: dict, res: dict) -> tuple[bool, str, str | None]:
    """Returns (matched, description, timestamp of the first matching record)."""
    alerts, nets = res.get("alerts", []), res.get("net_events", [])
    if "alert_rule" in expect:
        for a in alerts:
            if a["rule_id"] == expect["alert_rule"]:
                return True, f"alert {a['rule_id']}", a["created_at"]
    if "alert_rule_prefix" in expect:
        for a in alerts:
            if a["rule_id"].startswith(expect["alert_rule_prefix"]):
                return True, f"alert {a['rule_id']}", a["created_at"]
    if "net_blocked_host" in expect:
        h = expect["net_blocked_host"]
        for n in nets:
            if n["action"] == "blocked" and (n["host"] == h or n["host"].endswith("." + h)):
                return True, f"{n['layer']} blocked {n['host']}", n["occurred_at"]
    if "net_event" in expect:
        want = expect["net_event"]
        for n in nets:
            if all(n.get(k) == v for k, v in want.items()):
                return True, f"{n['layer']} {n['action']} {n['host']}", n["occurred_at"]
    if expect.get("any"):
        if alerts:
            return True, f"alert {alerts[0]['rule_id']}", alerts[0]["created_at"]
        blocked = [n for n in nets if n["action"] == "blocked"]
        if blocked:
            return True, f"blocked {blocked[0]['host']}", blocked[0]["occurred_at"]
    return False, "", None


def iso_ms(ts: str | None) -> int | None:
    if not ts:
        return None
    from datetime import datetime
    return int(datetime.fromisoformat(ts.replace("Z", "+00:00")).timestamp() * 1000)


def run(a: argparse.Namespace) -> int:
    doc = yaml.safe_load(pathlib.Path(a.scenarios).read_text())
    a.out.mkdir(parents=True, exist_ok=True)
    pc_ip = a.pc_ip or a.target.split("@")[-1]
    token = a.token or os.environ.get("XG_INTERNAL_TOKEN", "")
    results: list[Result] = []
    only = set(a.only.split(",")) if a.only else None
    for sc in doc["scenarios"]:
        if only and sc["id"] not in only:
            continue
        if sc.get("manual") or sc.get("layer") == "manual":
            results.append(Result(sc["id"], sc["layer"], sc["title"], "skipped", False, False, None, "manual scenario", "", 0))
            continue
        print(f"[{sc['id']}] {sc['title']}", flush=True)
        start = int(time.time() * 1000)
        try:
            _, out = ssh(a.target, sc["run"], sc.get("timeout_s", 120))
        except subprocess.TimeoutExpired:
            out = "timeout"
        if sc.get("manual_trigger") == "rescan" and a.rescan_cmd:
            subprocess.run(shlex.split(a.rescan_cmd.format(ip=pc_ip)), capture_output=True)
        blocked = blocked_check(sc.get("blocked_if"), out)
        detected, ev, when = False, "", None
        deadline = time.time() + a.wait
        while time.time() < deadline:
            detected, ev, when = evidence_match(sc.get("expect", {}), ccc_results(a.api, token, pc_ip, start))
            if detected:
                break
            time.sleep(1)
        if sc.get("cleanup"):
            try:
                ssh(a.target, sc["cleanup"], 60)
            except subprocess.TimeoutExpired:
                pass
        # CCC timestamps have 1 s resolution, so small negative differences are rounding: clamp to 0.
        ttd = max(0.0, (iso_ms(when) - start) / 1000) if when and iso_ms(when) else None
        outcome = "blocked" if blocked else ("detected" if detected else "missed")
        results.append(Result(sc["id"], sc["layer"], sc["title"], outcome, blocked, detected,
                              round(ttd, 2) if ttd is not None else None, ev, out[-200:], start))
        print(f"    -> {outcome.upper()} {ev} {'' if ttd is None else f'({ttd:.1f} s)'}", flush=True)
        time.sleep(a.pause)
    write(a.out, results)
    print(table_markdown(results))
    return 0


def write(out: pathlib.Path, results: list[Result]) -> None:
    with open(out / "results.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(asdict(results[0]).keys()) if results else ["id"])
        w.writeheader()
        for r in results:
            w.writerow(asdict(r))


def load(out: pathlib.Path) -> list[Result]:
    rows = []
    with open(out / "results.csv") as f:
        for r in csv.DictReader(f):
            rows.append(Result(r["id"], r["layer"], r["title"], r["outcome"], r["blocked"] == "True", r["detected"] == "True",
                               float(r["time_to_detect_s"]) if r["time_to_detect_s"] else None, r["evidence"], r["stdout"],
                               int(r["started_ms"] or 0)))
    return rows


def summary(results: list[Result]) -> dict:
    by_layer: dict[str, dict[str, int]] = {}
    for r in results:
        if r.outcome == "skipped":
            continue
        d = by_layer.setdefault(r.layer, {o: 0 for o in OUTCOMES} | {"n": 0})
        d[r.outcome] += 1
        d["n"] += 1
    tot = {o: sum(d[o] for d in by_layer.values()) for o in OUTCOMES} | {"n": sum(d["n"] for d in by_layer.values())}
    ttd = sorted(r.time_to_detect_s for r in results if r.time_to_detect_s is not None)
    return {"by_layer": by_layer, "total": tot,
            "ttd_median_s": ttd[len(ttd) // 2] if ttd else None, "ttd_max_s": ttd[-1] if ttd else None}


def table_markdown(results: list[Result]) -> str:
    s = summary(results)
    lines = ["| Layer | Attempts | Blocked | Detected | Missed | Bypass rate |", "|---|---:|---:|---:|---:|---:|"]
    for layer, d in sorted(s["by_layer"].items()) + [("**overall**", s["total"])]:
        rate = d["missed"] / d["n"] if d["n"] else 0
        lines.append(f"| {layer} | {d['n']} | {d['blocked']} | {d['detected']} | {d['missed']} | {rate:.0%} |")
    lines.append("")
    lines.append(f"Time to detect: median {s['ttd_median_s']} s, max {s['ttd_max_s']} s. "
                 "Bypass = neither blocked nor detected.")
    lines += ["", "| Scenario | Outcome | Evidence | TTD (s) |", "|---|---|---|---:|"]
    for r in results:
        lines.append(f"| {r.id} — {r.title} | {r.outcome} | {r.evidence} | {'' if r.time_to_detect_s is None else r.time_to_detect_s} |")
    return "\n".join(lines)


def table_latex(results: list[Result]) -> str:
    s = summary(results)
    rows = []
    for layer, d in sorted(s["by_layer"].items()) + [("Overall", s["total"])]:
        rate = d["missed"] / d["n"] if d["n"] else 0
        rows.append(f"{layer} & {d['n']} & {d['blocked']} & {d['detected']} & {d['missed']} & {100 * rate:.0f}\\% \\\\")
    return ("\\begin{tabular}{lrrrrr}\n\\toprule\nLayer & $n$ & Blocked & Detected & Missed & Bypass \\\\\n\\midrule\n"
            + "\n".join(rows) + "\n\\bottomrule\n\\end{tabular}\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--target", required=True, help="user@pc for SSH, or 'local'")
    r.add_argument("--pc-ip", help="lab IP of the target (default: host part of --target)")
    r.add_argument("--scenarios", required=True)
    r.add_argument("--out", type=pathlib.Path, required=True)
    r.add_argument("--api", default="http://127.0.0.1:8000")
    r.add_argument("--token", help="internal token (default: $XG_INTERNAL_TOKEN)")
    r.add_argument("--wait", type=float, default=15.0, help="seconds to wait for CCC evidence")
    r.add_argument("--pause", type=float, default=2.0)
    r.add_argument("--only", help="comma-separated scenario ids")
    r.add_argument("--rescan-cmd", default="docker compose exec -T web php artisan tinker --execute "
                   "\"app(App\\\\Services\\\\CommandBus::class)->send(App\\\\Models\\\\Pc::where('ip','{ip}')->first(),'rescan',['what'=>'all']);\"")
    rec = sub.add_parser("record")
    rec.add_argument("--out", type=pathlib.Path, required=True)
    rec.add_argument("id")
    rec.add_argument("outcome", choices=["blocked", "detected", "missed"])
    t = sub.add_parser("table")
    t.add_argument("--out", type=pathlib.Path, required=True)
    a = ap.parse_args()
    if a.cmd == "run":
        return run(a)
    if a.cmd == "record":
        rows = load(a.out)
        for x in rows:
            if x.id == a.id:
                x.outcome, x.blocked, x.detected, x.evidence = a.outcome, a.outcome == "blocked", a.outcome == "detected", "manual"
        write(a.out, rows)
        return 0
    rows = load(a.out)
    (a.out / "bypass_table.md").write_text(table_markdown(rows))
    (a.out / "bypass_table.tex").write_text(table_latex(rows))
    (a.out / "summary.json").write_text(json.dumps(summary(rows), indent=1))
    print(table_markdown(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
