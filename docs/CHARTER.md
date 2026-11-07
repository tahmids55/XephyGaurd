# The XephyGuard Integrity Charter

Every architectural decision, detection rule, enforcement action, and user-interface element in XephyGuard is governed by the ten principles below. Any change that violates this charter must be rejected in code review.

---

## 1. The Ten Principles

1. **Proportionality.** Collect only what the current mode needs. Collection outside an active session (`Always-on`) stays minimal (inventory only; no window focus, titles, keystroke timing, clipboard metrics, or work-folder activity).
2. **Prevent rather than surveil.** Blocking an AI domain at the gateway (DNS sinkhole, SNI filter, egress rules) is always preferable to watching a student use it or killing user applications after the fact.
3. **Truthful evidence.** Every recorded event represents something actually observed, tagged with its real source (`gateway`, `agent_process`, `agent_port`, `agent_extension`, `agent_file`, `helper_window`), confidence (`strong`, `medium`, `weak`), what the signal means, and what it does **not** mean. Nothing is synthesized or inferred and then stored as if directly observed.
4. **A human decides.** No automatic academic sanctions. Every score or alert is a *flag for human review* and explicitly states what the system could **not** observe (such as a personal phone not connected to the lab PC).
5. **Transparency.** Students can see what is collected, when collection changes (via the login banner, desktop tray indicator, and `/notice`), and the data recorded about them.
6. **Contestability.** Every flag can be explained in plain language, discussed with the student, and appealed, with an auditable record of the review and its outcome.
7. **Fail-safe.** If XephyGuard crashes, loses contact with the CCC, or encounters an error, the student's PC must keep working normally — no permanent network isolation, no unannounced browser termination, and no lost student work.
8. **Least privilege for staff.** Role-based access controls (`admin`, `instructor`, `assistant`), configurable two-person rules for intrusive actions, and an append-only audit log for every administrative or state-changing action.
9. **Data sovereignty.** All student and telemetry data stays on campus under the institution's control. There is no mandatory cloud dependency and no phone-home telemetry.
10. **Accessibility and fairness.** Student accommodations (screen readers, assistive tools, extra time) are first-class exemptions that suppress behavioral false positives, and detection rules are evaluated for disparate false-positive rates across student groups.

---

## 2. Mandatory Code-Review Checklist

Every pull request affecting `agent/`, `server/`, `proxy/`, `analysis/`, `data/`, or `installers/` must be verified against this checklist before merge:

### Proportionality & Collection (`ETHICS.md` Contract)
- [ ] Does any collector or `PolicyBuilder` change stay strictly within the tiered collection table in [`docs/ETHICS.md`](ETHICS.md)?
- [ ] Do both the server (`PolicyBuilder`) and the agent (`Policy::from_json` / collector start) clamp collection by mode (`always_on`, `practice`, `test`) as defence in depth?
- [ ] Is `title_capture` forced to `off` in `always_on`, clamped to at most `app_only` in `practice`, and only allowed to be `full` when explicitly configured in `test`?
- [ ] Does the change preserve the invariant that typed characters, clipboard text, file contents, screenshots, camera, and audio are **never** collected or transmitted?

### Truthful Evidence & Detection Quality
- [ ] Does every emitted event correspond to a real, directly observed system or network signal (never a fabricated `window_focus` or synthetic event)?
- [ ] Does the event payload accurately distinguish between **installed** software (inventory), **background/extension file activity**, **active process/port use**, and **blocked network attempts**?
- [ ] Are confidence levels (`strong`, `medium`, `weak`) assigned honestly, and do weak signals (such as window-title keyword matches or typing rhythm) include explicit `meaning` and `not_meaning` caveats?
- [ ] Does the agent refrain from modifying OS network caches (such as running `ipconfig /flushdns`) for detection purposes?

### Graduated & Fail-Safe Enforcement
- [ ] Are process kills (`kill` command) restricted exclusively to active **Test** sessions where the active profile explicitly enables `kill`?
- [ ] Does the code guarantee that browsers are **never** killed on window-title keyword matches or outside active Test sessions?
- [ ] Are student warning popups suppressed in `always_on` and deduplicated for installed-only inventory items so students are never harassed by repetitive alerts?
- [ ] Does network isolation use a bounded **isolation lease** (`isolation_lease_s`) renewed by the CCC, persist exact pre-isolation routes/firewall rules (`isolation_saved.json`), and automatically restore connectivity on lease expiry or agent restart?

### Security, Secrets & Governance
- [ ] Are TLS hostname/SAN verification and CA fingerprint pinning both enforced on all agent-to-CCC connections (no skipped hostname verification)?
- [ ] Are installer scripts (`installers/**`) and build artifacts completely free of hardcoded enrollment keys, CA fingerprints, passwords, or tokens?
- [ ] Are database schema or seed changes implemented through reproducible migrations/seeders and recorded in the audit log rather than direct database edits?
- [ ] Do all unit, feature, and ethics-contract tests pass in CI?
