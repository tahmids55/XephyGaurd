"""CLI: python -m xg_analysis {serve,score}. The research export is the server's (DECISIONS #30)."""
from __future__ import annotations

import argparse
import json
import sys

from .db import Db


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="xg_analysis")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("serve", help="run the HTTP scoring service")
    s = sub.add_parser("score", help="score one session and print the summary")
    s.add_argument("--session", type=int, required=True)
    s.add_argument("--v2", action="store_true")
    a = ap.parse_args(argv)
    if a.cmd == "serve":
        from .service import main as serve
        serve()
        return 0
    if a.cmd == "score":
        from .pipeline import score_session
        print(json.dumps(score_session(Db(), a.session, v2=a.v2), indent=1, default=str))
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
