# F112. Docker Compose stack and installer

- **Component:** ccc-deploy
- **Area:** 19. Packaging and deployment tools
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** one-command CCC install on Ubuntu: asks for interfaces, mode, IPs; writes `.env`, state
  dirs, CA; starts `mysql`, `web` (nginx + php-fpm, ports 80/443/8443), `reverb`, `worker`,
  `scheduler`, `netpolicy`, `proxy`, `analysis`, `discovery`.
- **Trigger:** `sudo ccc-deploy/install.sh`; `docker compose -f ccc-deploy/docker-compose.yml up -d`.
- **Code:** `ccc-deploy/install.sh`, `docker-compose.yml`, `docker/*.Dockerfile`, `nginx/xephyguard.conf`.
- **Status:** partly working (compose config valid; `install.sh` never run on a real two-NIC host).
- **Test:** 1. `docker compose -f ccc-deploy/docker-compose.yml config -q`. 2. On a spare VM, run `install.sh`.
