# Features: agent

agent/ — lab PC agent (root/SYSTEM service) and session helper

| ID | Feature | Area | Status |
|---|---|---|---|
| [F14](F014-agent-enrollment-request.md) | Agent enrollment request | 3. Enrollment and PC management | working |
| [F35](F035-kill-a-process.md) | Kill a process | 6. Commands to PCs | working |
| [F36](F036-isolate-a-pc-from-the-internet.md) | Isolate a PC from the Internet | 6. Commands to PCs | working on Linux |
| [F38](F038-ping-and-flush.md) | Ping and flush | 6. Commands to PCs | working |
| [F62](F062-agent-bundle-verification.md) | Agent bundle verification | 11. Signatures and blocklists | working |
| [F72](F072-heartbeat-and-capabilities.md) | Heartbeat and capabilities | 13. Agent: monitoring on lab PCs | working |
| [F73](F073-process-monitoring-and-ai-process-detection.md) | Process monitoring and AI process detection | 13. Agent: monitoring on lab PCs | working on Linux |
| [F74](F074-listening-ports-and-local-llm-api-probe.md) | Listening ports and local LLM API probe | 13. Agent: monitoring on lab PCs | working |
| [F75](F075-window-focus-and-titles.md) | Window focus and titles ⚠PRIV | 13. Agent: monitoring on lab PCs | working on X11 |
| [F76](F076-browser-ai-tab-detection.md) | Browser AI tab detection | 13. Agent: monitoring on lab PCs | partly working |
| [F77](F077-paste-and-copy-features.md) | Paste and copy features | 13. Agent: monitoring on lab PCs | working |
| [F78](F078-keystroke-timing-statistics.md) | Keystroke timing statistics ⚠PRIV | 13. Agent: monitoring on lab PCs | working |
| [F79](F079-file-activity-in-work-directories.md) | File activity in work directories ⚠PRIV | 13. Agent: monitoring on lab PCs | unclear |
| [F80](F080-ai-extension-inventory-and-activity.md) | AI extension inventory and activity | 13. Agent: monitoring on lab PCs | working |
| [F81](F081-local-ai-model-files.md) | Local AI model files | 13. Agent: monitoring on lab PCs | working |
| [F82](F082-network-adapters-vpn-and-tethering.md) | Network adapters, VPN and tethering | 13. Agent: monitoring on lab PCs | working |
| [F83](F083-usb-devices.md) | USB devices | 13. Agent: monitoring on lab PCs | working on Linux |
| [F84](F084-gpu-usage-sampling.md) | GPU usage sampling | 13. Agent: monitoring on lab PCs | working on Intel/AMD Linux |
| [F85](F085-tamper-detection.md) | Tamper detection | 13. Agent: monitoring on lab PCs | partly working |
| [F86](F086-raw-packet-capture-on-windows.md) | Raw packet capture on Windows ⚠PRIV | 13. Agent: monitoring on lab PCs | unclear |
| [F87](F087-kill-ai-processes-by-policy.md) | Kill AI processes by policy | 14. Agent: enforcement on lab PCs | working |
| [F88](F088-re-evaluate-on-mode-change.md) | Re-evaluate on mode change | 14. Agent: enforcement on lab PCs | working |
| [F89](F089-network-isolation-on-the-pc.md) | Network isolation on the PC | 14. Agent: enforcement on lab PCs | — |
| [F90](F090-exam-lockdown-on-windows.md) | Exam lockdown on Windows | 14. Agent: enforcement on lab PCs | unclear |
| [F91](F091-browser-policies-and-dns-settings-on-the-pc.md) | Browser policies and DNS settings on the PC | 14. Agent: enforcement on lab PCs | working on Linux |
| [F92](F092-agent-cli-and-service.md) | Agent CLI and service | 14. Agent: enforcement on lab PCs | working on Linux |
| [F93](F093-notifications-and-messages.md) | Notifications and messages | 15. Session helper (student desktop) | partly working |
| [F94](F094-monitoring-status-indicator.md) | Monitoring status indicator | 15. Session helper (student desktop) | working on X11 |
| [F95](F095-embedded-screen-streamer.md) | Embedded screen streamer ⚠PRIV | 15. Session helper (student desktop) | partly working |
| [F96](F096-helper-status-command.md) | Helper status command | 15. Session helper (student desktop) | unclear |
| [F97](F097-pac-and-proxy-settings-for-the-user.md) | PAC and proxy settings for the user | 15. Session helper (student desktop) | working |

## Also touches this component

| ID | Feature | Owner |
|---|---|---|
| [F8](../ccc-deploy/F008-discovery-service-udp.md) | Discovery service (UDP) | ccc-deploy |
| [F27](../server/F027-start-stop-and-switch-a-session.md) | Start, stop and switch a session | server |
| [F32](../server/F032-policy-delivery-to-agents.md) | Policy delivery to agents | server |
| [F33](../server/F033-student-notice-in-policy.md) | Student notice in policy | server |
| [F34](../server/F034-send-a-message-to-students.md) | Send a message to students | server |
| [F37](../server/F037-rescan.md) | Rescan | server |
| [F43](../screen/F043-live-screen-view-of-a-pc.md) | Live screen view of a PC ⚠PRIV ⚠SEC | screen |
| [F71](../server/F071-proxy-auto-config-pac.md) | Proxy auto-config (PAC) | server |
| [F115](../installers/F115-linux-agent-package-deb.md) | Linux agent package (.deb) | installers |
