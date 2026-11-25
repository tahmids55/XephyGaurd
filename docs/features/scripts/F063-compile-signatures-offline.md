# F63. Compile signatures offline

- **Component:** scripts
- **Area:** 11. Signatures and blocklists
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** validates `data/signatures/*.yaml` and writes a `signatures.json` (used for the Windows
  installer seed).
- **Trigger:** `python3 scripts/compile_signatures.py [--out file]`.
- **Status:** working (run today: 72 rules).
- **Test:** 1. Run it with `--out /tmp/s.json`. 2. It prints the rule count.
