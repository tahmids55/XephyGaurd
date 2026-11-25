# Features: server

server/ — Laravel CCC: agent API, dashboard, scheduler, reports

| ID | Feature | Area | Status |
|---|---|---|---|
| [F1](F001-first-run-setup-wizard.md) | First-run setup wizard | 1. CCC setup and operations | working |
| [F2](F002-bootstrap-command.md) | Bootstrap command | 1. CCC setup and operations | working |
| [F3](F003-internal-certificate-authority.md) | Internal certificate authority | 1. CCC setup and operations | working |
| [F4](F004-ccc-doctor-self-check.md) | CCC doctor (self-check) | 1. CCC setup and operations | unclear |
| [F5](F005-network-state-writer.md) | Network state writer | 1. CCC setup and operations | working |
| [F6](F006-enrollment-key-command.md) | Enrollment key command | 1. CCC setup and operations | working |
| [F7](F007-signature-publish-command.md) | Signature publish command | 1. CCC setup and operations | working |
| [F10](F010-login-and-logout.md) | Login and logout | 2. Staff accounts and access | working |
| [F11](F011-role-based-access.md) | Role-based access | 2. Staff accounts and access | working |
| [F12](F012-staff-accounts-management.md) | Staff accounts management | 2. Staff accounts and access | partly working |
| [F13](F013-dashboard-live-channel-auth.md) | Dashboard live channel auth | 2. Staff accounts and access | working |
| [F15](F015-approve-or-reject-enrollments.md) | Approve or reject enrollments | 3. Enrollment and PC management | working |
| [F16](F016-auto-approve-by-mac.md) | Auto-approve by MAC | 3. Enrollment and PC management | working |
| [F17](F017-rotate-enrollment-key-web.md) | Rotate enrollment key (web) | 3. Enrollment and PC management | working |
| [F18](F018-rename-a-pc.md) | Rename a PC | 3. Enrollment and PC management | unclear |
| [F19](F019-retire-a-pc.md) | Retire a PC | 3. Enrollment and PC management | working |
| [F20](F020-agent-authentication-mtls-token.md) | Agent authentication (mTLS + token) | 3. Enrollment and PC management | working |
| [F21](F021-agent-channel-auth-reverb.md) | Agent channel auth (Reverb) | 3. Enrollment and PC management | working |
| [F22](F022-seat-layout-editor.md) | Seat layout editor | 3. Enrollment and PC management | unclear |
| [F23](F023-live-lab-seat-map.md) | Live lab seat map | 4. Live lab dashboard | working |
| [F24](F024-pc-inspector-9-tabs.md) | PC inspector (9 tabs) | 4. Live lab dashboard | working |
| [F25](F025-health-classification-of-pcs.md) | Health classification of PCs | 4. Live lab dashboard | partly working |
| [F26](F026-synthetic-desktop-drawer-demo.md) | Synthetic desktop drawer (demo) | 4. Live lab dashboard | unclear |
| [F27](F027-start-stop-and-switch-a-session.md) | Start, stop and switch a session | 5. Sessions, profiles and timetable | working |
| [F28](F028-per-pc-profile-override.md) | Per-PC profile override | 5. Sessions, profiles and timetable | unclear |
| [F29](F029-profiles.md) | Profiles | 5. Sessions, profiles and timetable | partly working |
| [F30](F030-built-in-profiles.md) | Built-in profiles | 5. Sessions, profiles and timetable | working |
| [F31](F031-timetable.md) | Timetable | 5. Sessions, profiles and timetable | working |
| [F32](F032-policy-delivery-to-agents.md) | Policy delivery to agents | 5. Sessions, profiles and timetable | working |
| [F33](F033-student-notice-in-policy.md) | Student notice in policy | 5. Sessions, profiles and timetable | partly working |
| [F34](F034-send-a-message-to-students.md) | Send a message to students | 6. Commands to PCs | working |
| [F37](F037-rescan.md) | Rescan | 6. Commands to PCs | working |
| [F39](F039-alert-creation-from-events.md) | Alert creation from events | 7. Alerts and review | working |
| [F40](F040-alerts-list-and-filters.md) | Alerts list and filters | 7. Alerts and review | working |
| [F41](F041-acknowledge-alerts-single-and-batch.md) | Acknowledge alerts (single and batch) | 7. Alerts and review | working |
| [F42](F042-live-score-per-pc.md) | Live score per PC | 7. Alerts and review | working |
| [F44](F044-screen-view-lease-renew-and-stop.md) | Screen view lease renew and stop | 8. Screen viewing and evidence | working |
| [F46](F046-screenshot-evidence.md) | Screenshot evidence ⚠PRIV | 8. Screen viewing and evidence | working |
| [F49](F049-session-reports-list-and-detail-html.md) | Session reports list and detail (HTML) | 9. Scoring, reports and research | partly working |
| [F50](F050-student-report.md) | Student report | 9. Scoring, reports and research | unclear |
| [F51](F051-pdf-and-csv-export.md) | PDF and CSV export | 9. Scoring, reports and research | working |
| [F52](F052-research-export.md) | Research export | 9. Scoring, reports and research | working |
| [F53](F053-rescore.md) | Rescore | 9. Scoring, reports and research | unclear |
| [F54](F054-roster-import.md) | Roster import | 10. Roster and consent | unclear |
| [F55](F055-research-consent-per-student.md) | Research consent per student | 10. Roster and consent | working through F52's test |
| [F56](F056-exempt-a-student.md) | Exempt a student | 10. Roster and consent | working |
| [F57](F057-pseudonymise-roster.md) | Pseudonymise roster | 10. Roster and consent | unclear |
| [F58](F058-signature-and-blocklist-pages.md) | Signature and blocklist pages | 11. Signatures and blocklists | working |
| [F59](F059-edit-blocklist-entries.md) | Edit blocklist entries | 11. Signatures and blocklists | working |
| [F60](F060-edit-detection-rules.md) | Edit detection rules | 11. Signatures and blocklists | unclear |
| [F61](F061-publish-and-roll-back-signature-bundles.md) | Publish and roll back signature bundles | 11. Signatures and blocklists | working |
| [F71](F071-proxy-auto-config-pac.md) | Proxy auto-config (PAC) | 12. Network enforcement (gateway, DNS, proxy) | working |
| [F98](F098-student-notice.md) | Student notice | 16. Public pages | partly working |
| [F99](F099-downloads-and-install-commands.md) | Downloads and install commands ⚠SEC | 16. Public pages | working |
| [F100](F100-linux-install-script.md) | Linux install script | 16. Public pages | working |
| [F101](F101-ca-certificate-download.md) | CA certificate download | 16. Public pages | working |
| [F102](F102-shared-clipboard-text-share.md) | Shared clipboard / text share ⚠SEC ⚠PRIV | 16. Public pages | working |
| [F103](F103-health-page.md) | Health page | 17. Admin: health, settings, audit | working |
| [F104](F104-settings.md) | Settings | 17. Admin: health, settings, audit | working |
| [F105](F105-audit-log.md) | Audit log | 17. Admin: health, settings, audit | partly working |
| [F106](F106-internal-eval-results-endpoint.md) | Internal eval results endpoint | 17. Admin: health, settings, audit | unclear |
| [F107](F107-timetable-tick-every-minute.md) | Timetable tick — every minute | 18. Background jobs | working |
| [F108](F108-machine-silent-detection-every-5-seconds.md) | Machine-silent detection — every 5 seconds | 18. Background jobs | working |
| [F109](F109-retention-purge-daily-03-15.md) | Retention purge — daily 03:15 | 18. Background jobs | working |
| [F110](F110-signature-auto-publish-every-minute.md) | Signature auto-publish — every minute ⚠SEC | 18. Background jobs | working |
| [F111](F111-session-scoring-job-queue.md) | Session scoring job (queue) | 18. Background jobs | working |

## Also touches this component

| ID | Feature | Owner |
|---|---|---|
| [F9](../analysis/F009-analysis-scoring-service-http.md) | Analysis scoring service (HTTP) | analysis |
| [F14](../agent/F014-agent-enrollment-request.md) | Agent enrollment request | agent |
| [F35](../agent/F035-kill-a-process.md) | Kill a process | agent |
| [F36](../agent/F036-isolate-a-pc-from-the-internet.md) | Isolate a PC from the Internet | agent |
| [F43](../screen/F043-live-screen-view-of-a-pc.md) | Live screen view of a PC ⚠PRIV ⚠SEC | screen |
| [F48](../analysis/F048-session-scoring-v1-rules-v2-isolation-forest.md) | Session scoring (v1 rules + v2 Isolation Forest) | analysis |
| [F64](../proxy/F064-gateway-or-proxy-only-network-mode.md) | Gateway or proxy-only network mode | proxy |
| [F70](../proxy/F070-observe-allowed-ai-ide-traffic.md) | Observe allowed AI/IDE traffic ⚠PRIV | proxy |
