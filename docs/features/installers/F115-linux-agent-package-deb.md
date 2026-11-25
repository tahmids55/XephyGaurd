# F115. Linux agent package (.deb)

- **Component:** installers (also: agent)
- **Area:** 19. Packaging and deployment tools
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** CPack `.deb` with the agent, helper, systemd units, default config, postinst/prerm/postrm.
- **Trigger:** `cmake -S agent -B agent/build-rel -DCMAKE_BUILD_TYPE=Release -DXG_BUILD_TESTS=OFF && cmake --build agent/build-rel -j2 && (cd agent/build-rel && cpack)`.
- **Code:** `agent/CMakeLists.txt`, `agent/packaging/`, `installers/linux/*`.
- **Status:** unclear (CI builds it; not built today).
- **Test:** 1. Build it. 2. `sudo apt install ./xephyguard-agent_*.deb` in a VM.
