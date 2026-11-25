# F38. Ping and flush

- **Component:** agent
- **Area:** 6. Commands to PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> All commands go through `S/Services/CommandBus.php::send`, are stored in `commands`, broadcast
> on `private-pc.<id>` (`S/Events/AgentCommand.php`) and acknowledged in the next event batch
> (`command_result`). Agent side: `A/core/src/agent.cpp:1406` (`handle_command`).

**Feature:**

- **What:** makes the agent send its queued events now.
- **Trigger:** internal command types `ping`, `flush`.
- **Status:** working (agent command test).
- **Test:** covered by tests; not exposed as a button.
