# F36. Isolate a PC from the Internet

- **Component:** agent (also: server)
- **Area:** 6. Commands to PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> All commands go through `S/Services/CommandBus.php::send`, are stored in `commands`, broadcast
> on `private-pc.<id>` (`S/Events/AgentCommand.php`) and acknowledged in the next event batch
> (`command_result`). Agent side: `A/core/src/agent.cpp:1406` (`handle_command`).

**Feature:**

- **What:** cuts Internet access on one PC (LAN and CCC stay reachable) and closes AI apps.
- **Trigger:** `POST /pcs/{pc}/isolate`. Assistant and up.
- **Code:** `SessionController::isolate`, `SessionManager::isolate`; agent `isolate_notice` +
  policy `isolate`; Linux `A/platform/linux/network_isolator.cpp` (nft via `popen`), Windows
  `A/platform/windows/isolator_win.cpp`.
- **Status:** working on Linux (`isolate cuts the Internet (LAN + CCC kept)…`); Windows unclear.
- **Test:** 1. Isolate a PC. 2. On it, `curl https://example.com` fails and the CCC page still loads.
