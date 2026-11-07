# Setting up the CCC (central computer)

> Other options (Windows via WSL2, native Linux/Windows without Docker): see
> [RUNNING_THE_SERVER.md](RUNNING_THE_SERVER.md).

## You need
* A PC or small server with **Ubuntu Server 22.04 or 24.04 LTS**, 4+ cores, 8 GB RAM, 100 GB disk.
* **Two network cards** for gateway mode (recommended): one to the internet/campus (WAN), one to the
  lab switch (LAN).
* A fixed lab address for the CCC, e.g. `192.168.10.2`, and DHCP reservations for the lab PCs (so a
  PC keeps its IP). If the CCC is the lab's DHCP server, set its own address as gateway and DNS.

## Install (≈ 15 minutes)
```bash
git clone <repo> /opt/xephyguard        # or unpack xephyguard-ccc-<ver>.tar.gz there
cd /opt/xephyguard
sudo ./ccc-deploy/install.sh            # answers: WAN card, LAN card, CCC IP, subnet, lab name
```
The installer: installs Docker + nftables, enables IP forwarding, frees port 53, writes secrets to
`ccc-deploy/.env` (0600), builds and starts the containers, creates the lab CA, writes the gateway
firewall, and prints the dashboard URL and the CA fingerprint.

## First-run wizard
Open `https://<CCC IP>/`. Accept the certificate warning once (or install `http://<CCC IP>/ca.crt` on
your admin machine and compare the fingerprint). The wizard asks for: admin account, lab name, network
mode, lab subnet and CCC IP, raw-data retention, student notice text.

## Check it
```bash
sudo xephyguard-ccc doctor
```
Every line explains the problem and the fix in plain words (IP forwarding, firewall rules, DNS, proxy,
time sync, disk, containers, CA expiry, signatures, analysis service, silent PCs).

## Lab network
* **Gateway mode:** lab PCs get the CCC as **default gateway and DNS** (DHCP option 3 and 6). Nothing
  else to configure on the PCs — proxy settings on a PC cannot bypass the CCC.
* **Proxy-only mode** (fallback): configure the lab router to forward TCP 80/443 from the lab subnet to
  `<CCC IP>:8080` and to hand out the CCC as DNS server; block UDP 443 and TCP/UDP 853 at the router.

## Day to day
| Task | Command |
|---|---|
| Health check | `sudo xephyguard-ccc doctor` |
| Logs | `sudo xephyguard-ccc logs web` (or worker, proxy, netpolicy, analysis, reverb) |
| Backup (DB + CA + secrets) | `sudo xephyguard-ccc backup /mnt/backup` |
| Update | `git pull && sudo xephyguard-ccc update` |
| Enrollment key | `sudo xephyguard-ccc enroll-key` (`--rotate` to replace) |
| Partition the events table (large labs) | see `ccc-deploy/mysql/partition-events.sql` |
| Publish installers | `scripts/release.sh <ver> --publish-to /var/lib/xephyguard/storage/public/downloads` |
