# Development

Development happens on Linux. Each component builds and tests on its own.

| Component | Build / run | Tests |
|---|---|---|
| Agent (C++20) | `cmake -S agent -B build && cmake --build build -j` | `ctest --test-dir build` (Catch2, fake adapters in `agent/tests/fakes.hpp`) |
| Agent package | `cmake -S agent -B build-rel -DCMAKE_BUILD_TYPE=Release -DXG_BUILD_TESTS=OFF && cmake --build build-rel && (cd build-rel && cpack)` | install test: `docker run ... ubuntu:24.04 apt install ./xephyguard-agent_*.deb` |
| Windows adapters | MSVC build on Windows (see `installers/windows/README.md`); on Linux a MinGW syntax check (CI job `agent-windows-compile-check`) | `docs/WINDOWS_TEST_CHECKLIST.md` on a real machine |
| CCC server (Laravel 13) | `cd server && composer install && npm ci && npm run build && php artisan migrate && php artisan xephyguard:ca:init && php artisan xephyguard:bootstrap && php artisan serve` | `php artisan test` |
| Proxy + network policy | `pip install -r proxy/requirements.txt` | `pytest proxy/tests` (uses mitmproxy's test helpers) |
| Analysis | `pip install -r analysis/requirements.txt`; `python -m xg_analysis serve` | `pytest analysis/tests` (schema generated from the migrations: `scripts/gen_analysis_schema.sh`) |
| Eval tools | – | `pytest eval/tests` |
| Signatures | edit `data/signatures/*.yaml` | `python3 scripts/compile_signatures.py` validates; CCC: Blocklists & signatures → Publish |

Dependencies of the agent: OpenSSL 3, SQLite3, pthreads (system); nlohmann/json, toml++ and Catch2 are
fetched by CMake and pinned. Optional: X11 + Xfixes for the Linux helper.

## Running the agent against a local CCC (no Docker)
1. In `server/`: `php artisan serve --port=8000` and `php artisan reverb:start --port=8081`
   (set `REVERB_PORT=8081`).
2. Put an nginx with the `:8443` server block from `ccc-deploy/nginx/xephyguard.conf` in front
   (`proxy_pass http://127.0.0.1:8000` and set `X-SSL-Client-Verify` / `X-SSL-Client-Cert`).
3. Write an `agent.toml` with non-root paths (`data_dir`, `ca_file`, `ipc`, …) and run
   `xephyguard-agent enroll --config agent.toml --key <key> --ccc localhost`, approve in the dashboard,
   then `XG_IPC_ALLOW_ANY=1 xephyguard-agent run --config agent.toml --foreground`.

This is how the end-to-end tests in the project log were run (enrollment, mTLS, Reverb commands, mode
switch in 0.55 s, process alert in < 0.1 s, machine-silent alert after ~15 s, tamper alerts).

## Conventions
* Nothing about detections is hard-coded in the agent: rules are data (`data/signatures`), compiled
  and signed by the CCC.
* Privacy: content never leaves the function that measures it (see `paste.cpp`, `keystats.cpp`,
  `input_evdev.cpp`, `helper_win.cpp`). Keep it that way in reviews.
* Record design choices in `docs/DECISIONS.md`.
