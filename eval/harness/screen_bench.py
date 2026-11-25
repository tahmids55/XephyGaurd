#!/usr/bin/env python3
"""Screen streaming benchmark wrapper (eval-10 / screen Step 20).

Builds the gateway and the screenbench bridge, starts the real gateway (-dev-insecure, loopback only, free port),
optionally an Xvfb display, runs `xgstreambench` through the gateway and a headless viewer for each workload and
stores one JSON file in eval/results/screen/. Needs `xephyguard-screen/build/xgstreambench` already built
(see docs/DEVELOPMENT.md) and `go`; the xvfb profile also needs `Xvfb`.

    screen_bench.py --seconds 10                       # synthetic capture, streamer pinned to CPU 0
    screen_bench.py --profile xvfb --workload typing   # real X11 capture on Xvfb (CPU-only worst case)
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCREEN = ROOT / "xephyguard-screen"
WORKLOADS = ["idle", "static", "typing", "scrolling", "drag", "video"]


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def screenbench_cmd(sb: str, gw: str, bench: str, workload: str, seconds: int, capture: str, cpu: str) -> list[str]:
    cmd = [sb, "-gateway", gw, "-bench", bench, "-workload", workload, "-seconds", str(seconds), "-capture", capture]
    if cpu:
        cmd += ["-cpu", cpu]
    return cmd


def table(rows: list[dict]) -> str:
    head = f"{'workload':<10} {'fps':>6} {'kB/s':>8} {'lat p50':>8} {'lat p95':>8} {'frames':>7} {'drops':>6} {'cpu%':>6} {'wake/s':>7}"
    out = [head]
    for r in rows:
        s = r.get("streamer") or {}
        out.append(f"{r['workload']:<10} {s.get('fps', 0):>6.1f} {r.get('viewer_bytes_per_s', 0) / 1000:>8.1f} "
                   f"{r.get('latency_ms_p50', 0):>8.1f} {r.get('latency_ms_p95', 0):>8.1f} {r.get('viewer_frames', 0):>7} "
                   f"{r.get('drops', 0):>6} {100 * s.get('cpu_s_per_s', 0):>6.1f} {s.get('wakeups_per_s', 0):>7.1f}")
    return "\n".join(out)


def wait_health(url: str, tries: int = 50) -> bool:
    for _ in range(tries):
        try:
            with urllib.request.urlopen(url, timeout=1):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def run(args: argparse.Namespace) -> int:
    bench = pathlib.Path(args.bench or SCREEN / "build" / "xgstreambench")
    if not bench.is_file():
        print(f"missing {bench}: build xephyguard-screen first (docs/DEVELOPMENT.md)", file=sys.stderr)
        return 2
    capture = "x11" if args.profile == "xvfb" else "synthetic"
    if capture == "x11" and not shutil.which("Xvfb"):
        print("profile xvfb needs Xvfb", file=sys.stderr)
        return 2
    work = pathlib.Path(tempfile.mkdtemp(prefix="xg-screen-bench-"))
    procs: list[subprocess.Popen] = []
    try:
        gwbin, sbbin = str(work / "gateway"), str(work / "screenbench")
        subprocess.run(["go", "build", "-o", gwbin, "."], cwd=SCREEN / "gateway", check=True)
        subprocess.run(["go", "build", "-o", sbbin, "./cmd/screenbench"], cwd=SCREEN / "gateway", check=True)
        gw = f"127.0.0.1:{free_port()}"
        procs.append(subprocess.Popen([gwbin, "-dev-insecure", "-addr", gw], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        if not wait_health(f"http://{gw}/health"):
            print("gateway did not start", file=sys.stderr)
            return 1
        env = dict(os.environ)
        if capture == "x11":
            display = f":{90 + os.getpid() % 100}"
            procs.append(subprocess.Popen(["Xvfb", display, "-screen", "0", "1920x1080x24", "-nolisten", "tcp"],
                                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
            env["DISPLAY"] = display
            time.sleep(1.5)
        rows = []
        for w in args.workload or WORKLOADS:
            res = subprocess.run(screenbench_cmd(sbbin, gw, str(bench), w, args.seconds, capture, args.cpu),
                                 capture_output=True, text=True, env=env, timeout=args.seconds + 60)
            if res.returncode != 0 or not res.stdout.strip():
                print(f"{w}: failed: {res.stderr.strip()}", file=sys.stderr)
                return 1
            rows.append(json.loads(res.stdout.strip().splitlines()[-1]))
        out = pathlib.Path(args.out)
        out.mkdir(parents=True, exist_ok=True)
        path = out / f"screen_bench_{args.profile}_{time.strftime('%Y%m%d_%H%M%S')}.json"
        meta = {"profile": args.profile, "capture": capture, "cpu_pinned": args.cpu or None, "seconds": args.seconds,
                "cpus": os.cpu_count(), "latency": "capture return to viewer receipt through the real gateway; no pixel decode or paint"}
        path.write_text(json.dumps({"meta": meta, "rows": rows}, indent=1) + "\n")
        print(table(rows))
        print(f"wrote {path}")
        return 0
    finally:
        for p in procs:
            p.terminate()
        for p in procs:
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
        shutil.rmtree(work, ignore_errors=True)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profile", choices=["native", "xvfb"], default="native")
    ap.add_argument("--workload", action="append", choices=WORKLOADS, help="repeatable; default all")
    ap.add_argument("--seconds", type=int, default=10)
    ap.add_argument("--cpu", default="0", help="CPU list for taskset on the streamer; empty = unpinned")
    ap.add_argument("--bench", help="path of xgstreambench (default xephyguard-screen/build/xgstreambench)")
    ap.add_argument("--out", default=str(ROOT / "eval" / "results" / "screen"))
    return run(ap.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
