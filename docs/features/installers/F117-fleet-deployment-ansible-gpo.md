# F117. Fleet deployment (Ansible, GPO)

- **Component:** installers
- **Area:** 19. Packaging and deployment tools
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **Code:** `installers/ansible/site.yml` + roles, `installers/gpo/README.md`, `new_setup_scripts/windows/deploy-agents.ps1`.
- **Status:** unclear.
- **Test:** 1. `ansible-playbook -i inventory.ini installers/ansible/site.yml --check`.
