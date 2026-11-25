# F37. Rescan

- **Component:** server (also: agent)
- **Area:** 6. Commands to PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> All commands go through `S/Services/CommandBus.php::send`, are stored in `commands`, broadcast
> on `private-pc.<id>` (`S/Events/AgentCommand.php`) and acknowledged in the next event batch
> (`command_result`). Agent side: `A/core/src/agent.cpp:1406` (`handle_command`).

**Feature:**

- **What:** asks a PC to rescan processes, extensions/installed, artifacts, network/ports/browser,
  or devices now.
- **Trigger:** `POST /pcs/{pc}/rescan` (`what`, `kind`). Assistant and up.
- **Code:** `SessionController::rescan`; agent `rescan`.
- **Status:** working (`rescan_endpoint_dispatches_specific_target_for_what_and_kind`).
- **Test:** 1. Click Rescan on the Installed tab. 2. The tab refreshes within ~15 s.
