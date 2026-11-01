"""HTTP service used by the CCC (ScoreSession job).

    POST /score   {"session_id": 12}   (Authorization: Bearer $XG_INTERNAL_TOKEN)
    GET  /health
"""
from __future__ import annotations

import hmac
import json
import logging
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from .db import Db
from .pipeline import score_session

log = logging.getLogger("xg-analysis")


class Handler(BaseHTTPRequestHandler):
    server_version = "xephyguard-analysis/0.1"

    def _send(self, code: int, body: dict) -> None:
        data = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self) -> None:  # noqa: N802
        if self.path == "/health":
            ok = Db().ping()
            self._send(200 if ok else 503, {"ok": ok})
        else:
            self._send(404, {"error": "not found"})

    def do_POST(self) -> None:  # noqa: N802
        token = os.environ.get("XG_INTERNAL_TOKEN", "")
        given = self.headers.get("Authorization", "").removeprefix("Bearer ").strip()
        if not token or not hmac.compare_digest(token, given):
            return self._send(401, {"error": "unauthorized"})
        if self.path != "/score":
            return self._send(404, {"error": "not found"})
        try:
            n = int(self.headers.get("Content-Length", "0"))
            body = json.loads(self.rfile.read(min(n, 65536)) or b"{}")
            sid = int(body["session_id"])
            self._send(200, score_session(Db(), sid))
        except KeyError as e:
            self._send(404, {"error": str(e)})
        except (ValueError, TypeError) as e:
            self._send(422, {"error": str(e)})
        except Exception as e:  # noqa: BLE001
            log.exception("scoring failed")
            self._send(500, {"error": str(e)})

    def log_message(self, fmt: str, *args) -> None:
        log.info("%s - %s", self.address_string(), fmt % args)


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    port = int(os.environ.get("XG_ANALYSIS_PORT", "8090"))
    log.info("analysis service on :%d", port)
    ThreadingHTTPServer(("0.0.0.0", port), Handler).serve_forever()


if __name__ == "__main__":
    main()
