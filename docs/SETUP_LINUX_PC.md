# Adding Linux lab PCs (Ubuntu / Debian)

**One PC:** on the dashboard open **Add a PC**, copy the one-liner, run it on the PC as root:
```bash
curl -fsSL http://192.168.10.2/install/linux.sh | sudo bash -s -- --key XG-XXXXXX-XXXXXX-XXXXXX
```
It downloads and verifies the lab CA (fingerprint check), installs the `.deb`, enrolls, and waits.
The PC appears as **Pending** on the dashboard → click **Approve** → name the seat on the seat map.

**Manual:** `sudo apt install ./xephyguard-agent_<ver>_amd64.deb` then
`sudo xephyguard-agent enroll --key <KEY> [--ccc 192.168.10.2] [--ca-fingerprint <sha256>]`.

**Many PCs:** `installers/ansible` (one inventory file):
```bash
ansible-playbook -i inventory.ini installers/ansible/site.yml \
  -e xephyguard_enroll_key=XG-... -e xephyguard_ca_fingerprint=<sha256>
```
Tip: enable **auto-approve** for the lab's MAC addresses (Add a PC page) before a mass rollout.

## What gets installed
| Path | Purpose |
|---|---|
| `/usr/lib/xephyguard/xephyguard-agent` (root, 0755) | the service, `xephyguard-agent.service` (`Restart=always`, `WatchdogSec=30`) |
| `/usr/lib/xephyguard/xephyguard-helper` | per-session helper (systemd user unit, enabled globally, + XDG autostart) |
| `/etc/xephyguard/agent.toml` (root, 0600) | configuration; edits raise a tamper alert |
| `/etc/xephyguard/manifest.sha256` | expected binary hashes (tamper check) |
| `/var/lib/xephyguard/queue.db` | offline event buffer |
| `/var/log/xephyguard/agent.log` | log |
| system CA store, Chromium NSS, Firefox policy | lab CA trust |
| `/etc/opt/chrome/…`, `/etc/opt/edge/…`, `/etc/firefox/policies/` | managed browser policies (AI features + DoH off) |

## Session type
X11 sessions give full window and clipboard visibility. On **Wayland** the compositor hides other
windows and the clipboard; key timing still works (the root service reads evdev). The dashboard shows
what each PC can and cannot see (detail drawer → "Can see"). For labs, prefer X11 (the Ansible role
sets `WaylandEnable=false` for GDM).

## Lab hardening (recommended)
* Students are standard users without sudo.
* VS Code from a root-owned extensions directory (`--extensions-dir /opt/xephyguard/vscode-extensions`).
* Docker/Podman not available to students (or monitored: `vm.container` rule); no `ollama` package.
* No unmanaged package installs; BIOS password and fixed boot order.

## Check
`sudo xephyguard-agent status` · `systemctl status xephyguard-agent` · `sudo xephyguard-agent scan`
(one-shot local scan that prints what would be flagged).
