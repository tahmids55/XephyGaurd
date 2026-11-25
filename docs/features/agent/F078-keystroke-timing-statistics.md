# F78. Keystroke timing statistics ⚠PRIV

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **What:** per window of `keystats_window_s`, counts and timing of key presses, never which keys.
- **Events:** `key_stats`.
- **Code:** `A/core/src/keystats.cpp`, `A/platform/linux/input_evdev.cpp`, Windows helper.
- **Status:** working (`key stats aggregate timing without key identity`).
- **Test:** 1. Type for a minute in a session with `keys` on. 2. `key_stats` rows exist for that PC.
