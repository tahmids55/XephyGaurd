# F87. Kill AI processes by policy

- **Component:** agent
- **Area:** 14. Agent: enforcement on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** in Test mode or while isolated, high/critical process hits are killed automatically,
  except an exempt list.
- **Code:** agent core (`actions.kill`, `kill_exempt`).
- **Status:** working (`agent kills when the policy opts in, but never exempt processes`).
  Broad exe names (`aide`, `comet`, `copilot`) can close unrelated programs.
- **Test:** 1. Start a Test session. 2. Run a program matching a kill rule. 3. It closes.
