# XephyGuard — lab assistant guide (one page)

**Dashboard:** `https://<CCC address>/` · **Health:** `sudo xephyguard-ccc doctor`

| I want to… | Do this |
|---|---|
| Add a PC | **Add a PC** → run the shown command (Linux) or installer (Windows) on the PC → click **Approve** → rename it (e.g. `A3`) |
| Place PCs on the seat map | **Live lab** → **Edit seat map** → drag tiles onto seats → **Save seat map** |
| Start a class or test now | **Live lab** → course name → profile (*Practice…* or *Test: AI blocked*) → optional minutes → **Start session** |
| Use the timetable instead | **Timetable & profiles** → add day/time/course/profile. Sessions start and stop by themselves |
| Switch Practice ↔ Test | **Live lab** → choose profile → **Switch** (PCs follow within seconds) |
| End the session | **End session** (blocking relaxes immediately; the report is generated automatically) |
| Tell everyone something | **Message all** (or a single PC from its drawer) |
| Look at a PC | Click its tile → timeline, alerts, processes, network, typing chart, pastes |
| Cut one PC off the network | PC drawer → **Isolate** (agent stays connected) → **Restore network** later |
| Close a program on a PC | PC drawer → **Close a program…** → executable name (e.g. `ollama`) |
| Give a student an accommodation | **Sessions** → per-PC override → choose profile + reason (logged) |
| Mark an alert as reviewed | **Alerts** → comment → **Ack** |

Tile colours: **green** clean · **amber** warning · **red** alert · **grey** offline. The number is a
live risk indicator, not a verdict. **Every alert is a flag for human review.**

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| PC never appears as Pending | wrong key, CCC not reachable, CA fingerprint mismatch | rerun the install command; check `xephyguard-agent status`; rotate key if leaked |
| Tile grey (offline) | PC off, agent stopped, network cut | if a session is running you also get a *Machine silent* alert; check the PC |
| "Helper not running" alert | student closed the helper, or Wayland/RDP oddity | it restarts automatically; if repeated, log the student out and in |
| PC "cannot see" window/clipboard | Linux Wayland session | use an X11 session for labs |
| Sites not blocked during Test | PC uses another gateway/DNS, or not in the session | check DHCP gives the CCC as gateway + DNS; `doctor`; restart session |
| Browser certificate warnings on PCs | lab CA not installed in that browser | reinstall the agent package / installer (it installs the CA) |
| Google still shows AI Overview | PC does not trust the lab CA, or Google changed markup | install CA; admin: update fallback selectors in Settings |
| Dashboard not updating live | WebSocket blocked | reload; `xephyguard-ccc logs reverb` |
| Many false "large paste" alerts | assignment template pasted at start | early pastes are discounted; raise the threshold in Settings |
| Report says "scoring failed" | analysis service down | `xephyguard-ccc logs analysis`, then **Re-score** on the report |
