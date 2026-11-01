# Managed browser policies

Installed by the agent packages (Linux: `/etc/opt/chrome/policies/managed`, `/etc/chromium/...`,
`/etc/opt/edge/...`, `/etc/firefox/policies/policies.json`; Windows: registry via the NSIS installer
or GPO/Intune). Purpose: disable built-in AI features and browser-level DNS-over-HTTPS, and trust the
lab CA.

**Vendors rename and add AI policies frequently. Verify every name below against the current
documentation before each semester** and record the check in `docs/DECISIONS.md`:

* Chrome: https://chromeenterprise.google/policies/
* Edge: https://learn.microsoft.com/deployedge/microsoft-edge-policies
* Firefox: https://mozilla.github.io/policy-templates/

Unknown policy names are ignored by browsers, so keeping retired names is harmless.

| Policy | Browser | Effect |
|---|---|---|
| `DnsOverHttpsMode = off` | Chrome/Edge | no DoH; DNS must go to the CCC resolver |
| `GenAiDefaultSettings = 2` | Chrome | disable all generative AI features by default |
| `GeminiSettings = 1` | Chrome | disable Gemini in Chrome |
| `HelpMeWriteSettings`, `TabOrganizerSettings`, `TabCompareSettings`, `HistorySearchSettings`, `DevToolsGenAiSettings`, `CreateThemesSettings` = 2 | Chrome | disable individual AI features |
| `GenAILocalFoundationalModelSettings = 1` | Chrome | do not download the on-device model |
| `HubsSidebarEnabled = false` | Edge | remove the Copilot sidebar |
| `CopilotPageContext`, `CopilotCDPPageContext`, `EdgeEntraCopilotPageContext = false` | Edge | Copilot may not read pages |
| `ComposeInlineEnabled = false`, `Microsoft365CopilotChatIconEnabled = false`, `EdgeHistoryAISearchEnabled = false` | Edge | other Copilot entry points |
| `DNSOverHTTPS.Enabled = false (locked)` | Firefox | no DoH |
| `GenerativeAI.Enabled = false (locked)` | Firefox | disable AI chatbot sidebar / AI features |
| `Certificates.Install` + `ImportEnterpriseRoots` | Firefox | trust the lab CA |
