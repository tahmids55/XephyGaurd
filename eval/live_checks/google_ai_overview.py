#!/usr/bin/env python3
"""Periodic check that Google AI Overviews are still suppressed (search markup changes often).

Run from a lab PC that is in a Test session (traffic goes through the CCC proxy), or anywhere with
--direct to check that the udm=14 mechanism itself still yields a page without the AI block:

    google_ai_overview.py                # through the lab proxy: expects the request to be rewritten
    google_ai_overview.py --direct       # sends udm=14 itself and checks the response
Exit code 0 = suppressed, 1 = AI Overview markers found, 2 = could not check (network/consent page).
Schedule weekly (cron) and alert the admin on non-zero exit codes.
"""
import argparse
import re
import sys
import urllib.parse
import urllib.request

QUERIES = ["how does quicksort work", "what is a binary search tree", "explain recursion in python"]
# Markers seen in AI Overview markup; keep in sync with Settings -> fallback CSS selectors.
MARKERS = [r"AI Overview", r'data-attrid="AIOverview"', r'jsname="txosbe"', r"g-ai-overview", r'id="m-x-content"']
UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:140.0) Gecko/20100101 Firefox/140.0"


def fetch(q: str, direct: bool) -> tuple[str, str]:
    params = {"q": q, "hl": "en"}
    if direct:
        params["udm"] = "14"
    req = urllib.request.Request("https://www.google.com/search?" + urllib.parse.urlencode(params), headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=15) as r:
        return r.geturl(), r.read().decode("utf-8", "replace")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--direct", action="store_true")
    a = ap.parse_args()
    worst = 0
    for q in QUERIES:
        try:
            url, html = fetch(q, a.direct)
        except OSError as e:
            print(f"? {q}: {e}")
            worst = max(worst, 2)
            continue
        if "consent.google" in url or "Before you continue" in html:
            print(f"? {q}: consent interstitial, cannot evaluate")
            worst = max(worst, 2)
            continue
        hits = [m for m in MARKERS if re.search(m, html)]
        print(f"{'✘' if hits else '✔'} {q}: {'markers ' + ', '.join(hits) if hits else 'no AI Overview markers'}")
        worst = max(worst, 1 if hits else 0)
    return 1 if worst == 1 else worst


if __name__ == "__main__":
    sys.exit(main())
