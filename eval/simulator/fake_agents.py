#!/usr/bin/env python3
"""Fake-agent load simulator: N virtual PCs enroll (with real CSRs and client certificates) and then
stream heartbeats and realistic events to the CCC, measuring ingest latency.

Target from the spec: 100 PCs, events visible on the dashboard in under 1 s.

    # 1. on the dashboard enable auto-approve and paste the MACs printed by --print-macs
    fake_agents.py --ccc 192.168.10.2 --key XG-... --n 100 --print-macs
    # 2. run
    fake_agents.py --ccc 192.168.10.2 --key XG-... --n 100 --minutes 10 --out eval/results/load.json

Requires the `openssl` CLI (for keys/CSRs). Event mix per PC every 5 s: heartbeat + key_stats, plus
occasional window_focus, paste and rule_hit events.
"""
from __future__ import annotations

import argparse
import http.client
import json
import pathlib
import random
import ssl
import statistics
import subprocess
import tempfile
import threading
import time

API = "/api/agent/v1"


def mac(i: int) -> str:
    return "02:58:47:%02x:%02x:%02x" % ((i >> 16) & 255, (i >> 8) & 255, i & 255)


class VirtualPc:
    def __init__(self, i: int, a: argparse.Namespace, workdir: pathlib.Path):
        self.i, self.a, self.dir = i, a, workdir / f"pc{i:03d}"
        self.dir.mkdir(parents=True, exist_ok=True)
        self.seq = 0
        self.token = ""
        self.pc_id = ""
        self.latencies: list[float] = []
        self.errors = 0

    def ctx(self, client: bool) -> ssl.SSLContext:
        c = ssl.create_default_context(cafile=str(self.a.ca))
        c.check_hostname = False if self.a.insecure_hostname else True
        if client:
            c.load_cert_chain(self.dir / "agent.crt", self.dir / "agent.key")
        return c

    def req(self, method: str, path: str, body: dict | None = None, client: bool = True) -> tuple[int, dict]:
        conn = http.client.HTTPSConnection(self.a.ccc, self.a.port, context=self.ctx(client), timeout=15)
        headers = {"Content-Type": "application/json", "Accept": "application/json"}
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        conn.request(method, path, json.dumps(body) if body is not None else None, headers)
        r = conn.getresponse()
        data = r.read()
        conn.close()
        try:
            return r.status, json.loads(data or b"{}")
        except ValueError:
            return r.status, {}

    def enroll(self) -> bool:
        key, csr = self.dir / "agent.key", self.dir / "agent.csr"
        if (self.dir / "creds.json").exists():
            c = json.loads((self.dir / "creds.json").read_text())
            self.token, self.pc_id = c["token"], str(c["pc_id"])
            return True
        subprocess.run(["openssl", "req", "-new", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes",
                        "-keyout", str(key), "-out", str(csr), "-subj", f"/CN=sim-pc-{self.i:03d}"], check=True, capture_output=True)
        st, r = self.req("POST", API + "/enroll", {"enroll_key": self.a.key, "hostname": f"sim-pc-{self.i:03d}", "os": "Simulator",
                                                   "macs": [mac(self.i)], "csr_pem": csr.read_text(), "agent_version": "sim"}, client=False)
        if st != 201:
            print(f"pc{self.i}: enroll failed {st} {r}")
            return False
        for _ in range(120):
            st, p = self.req("GET", f"{API}/enroll/{r['enrollment_id']}?poll_token={r['poll_token']}", client=False)
            if p.get("status") == "approved":
                (self.dir / "agent.crt").write_text(p["cert_pem"])
                self.token, self.pc_id = p["token"], str(p["pc_id"])
                (self.dir / "creds.json").write_text(json.dumps({"token": self.token, "pc_id": self.pc_id}))
                return True
            time.sleep(2)
        print(f"pc{self.i}: not approved (enable auto-approve for the simulator MACs)")
        return False

    def event(self, type_: str, data: dict) -> dict:
        self.seq += 1
        return {"seq": self.seq, "ts": int(time.time() * 1000), "type": type_, "data": data}

    def loop(self, until: float) -> None:
        user = f"sim{self.i:03d}"
        while time.time() < until:
            ev = [self.event("key_stats", {"window_start": int(time.time() * 1000) - 5000, "window_s": 5, "keys": random.randint(0, 40),
                                           "backspaces": random.randint(0, 4), "iki_mean": random.uniform(90, 250), "bursts": random.randint(0, 2),
                                           "paste_chords": 0, "user": user})]
            if random.random() < 0.2:
                ev.append(self.event("window_focus", {"app": random.choice(["code", "firefox", "gnome-terminal-server"]), "title": "", "user": user}))
            if random.random() < 0.03:
                ev.append(self.event("paste", {"chars": random.randint(20, 1500), "lines": random.randint(1, 50), "code": True, "score": 0.7,
                                               "src": "firefox", "dst": "code", "user": user}))
            if random.random() < 0.005:
                ev.append(self.event("rule_hit", {"rule_id": "proc.ollama", "severity": "high", "category": "local_llm", "description": "Ollama",
                                                  "subject": "ollama", "actions": ["log", "warn", "alert"], "user": user}))
            body = {"v": 1, "pc_id": self.pc_id, "events": ev,
                    "heartbeat": {"mode": "test", "user": user, "agent_version": "sim", "local_time_ms": int(time.time() * 1000)}}
            t0 = time.perf_counter()
            try:
                st, _ = self.req("POST", API + "/events", body)
                if st == 200:
                    self.latencies.append(time.perf_counter() - t0)
                else:
                    self.errors += 1
            except OSError:
                self.errors += 1
            time.sleep(max(0.0, 5 - (time.perf_counter() - t0)) + random.uniform(-0.3, 0.3))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ccc", required=True)
    ap.add_argument("--port", type=int, default=8443)
    ap.add_argument("--key", required=True)
    ap.add_argument("--ca", type=pathlib.Path, required=True, help="lab CA certificate (http://<ccc>/ca.crt)")
    ap.add_argument("--n", type=int, default=100)
    ap.add_argument("--minutes", type=float, default=5)
    ap.add_argument("--workdir", type=pathlib.Path, default=pathlib.Path(tempfile.gettempdir()) / "xg-sim")
    ap.add_argument("--out", type=pathlib.Path)
    ap.add_argument("--print-macs", action="store_true")
    ap.add_argument("--insecure-hostname", action="store_true", help="skip hostname check (CA is still verified)")
    a = ap.parse_args()
    if a.print_macs:
        print("\n".join(mac(i) for i in range(a.n)))
        return 0
    pcs = [VirtualPc(i, a, a.workdir) for i in range(a.n)]
    ok = [p for p in pcs if p.enroll()]
    print(f"{len(ok)}/{a.n} virtual PCs enrolled; streaming for {a.minutes} min")
    until = time.time() + a.minutes * 60
    threads = [threading.Thread(target=p.loop, args=(until,), daemon=True) for p in ok]
    for t in threads:
        t.start()
        time.sleep(5 / max(1, len(threads)))   # spread over the heartbeat interval
    for t in threads:
        t.join()
    lat = sorted(x for p in ok for x in p.latencies)
    res = {"pcs": len(ok), "requests": len(lat), "errors": sum(p.errors for p in ok),
           "p50_ms": round(1000 * statistics.median(lat), 1) if lat else None,
           "p95_ms": round(1000 * lat[int(0.95 * (len(lat) - 1))], 1) if lat else None,
           "max_ms": round(1000 * lat[-1], 1) if lat else None,
           "target": "events reach the CCC (and the dashboard broadcast) in < 1000 ms"}
    print(json.dumps(res, indent=1))
    if a.out:
        a.out.parent.mkdir(parents=True, exist_ok=True)
        a.out.write_text(json.dumps(res, indent=1))
    return 0 if res["p95_ms"] is not None and res["p95_ms"] < 1000 else 1


if __name__ == "__main__":
    raise SystemExit(main())
