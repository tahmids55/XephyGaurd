# F119. Red-team harness

- **Component:** eval
- **Area:** 20. Evaluation tools
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** runs scripted bypass attempts (`eval/scenarios/linux.yaml`, `windows.yaml`) against a PC
  and checks the CCC caught them.
- **Trigger:** `python eval/harness/redteam.py --scenarios … --api … --token …`.
- **Status:** working (`eval/tests`, 4 tests).
- **Test:** 1. `python3 -m pytest -q eval/tests`.
