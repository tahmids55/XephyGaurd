# F34. Send a message to students

- **Component:** server (also: agent)
- **Area:** 6. Commands to PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> All commands go through `S/Services/CommandBus.php::send`, are stored in `commands`, broadcast
> on `private-pc.<id>` (`S/Events/AgentCommand.php`) and acknowledged in the next event batch
> (`command_result`). Agent side: `A/core/src/agent.cpp:1406` (`handle_command`).

**Feature:**

- **Trigger:** `POST /sessions/message` (`text`, optional `pc_ids`, `urgent`). Assistant and up.
- **Code:** `SessionController::message`; agent `message` → helper `notify`.
- **Side effects:** command row; audit; tray notification plus a modal dialog on Windows.
- **Status:** working (`commands_are_executed_and_acknowledged`, agent test).
- **Test:** 1. Send "hello" to one PC. 2. The student sees it.
