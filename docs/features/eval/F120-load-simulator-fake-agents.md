# F120. Load simulator (fake agents)

- **Component:** eval
- **Area:** 20. Evaluation tools
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **Trigger:** `python eval/simulator/fake_agents.py --ccc … --n 50 --minutes 5 --key … --ca …`.
- **Status:** unclear. Heavy: do not run large `--n` on the 8 GB machine.
- **Test:** 1. `--n 2 --minutes 1` against a local CCC.
