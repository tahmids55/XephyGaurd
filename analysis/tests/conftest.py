import json
import pathlib
import sys
from datetime import datetime, timedelta, timezone

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from xg_analysis.db import Db  # noqa: E402

SCHEMA = (pathlib.Path(__file__).parent / "schema.sql").read_text()
T0 = datetime(2026, 11, 2, 9, 0, tzinfo=timezone.utc)


def ts(minutes: float) -> str:
    return (T0 + timedelta(minutes=minutes)).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


class LabBuilder:
    """Inserts synthetic, realistic session data through the real schema."""

    def __init__(self, db: Db):
        self.db = db
        self.c = db.conn
        self.c.execute("INSERT INTO labs (id, name, grid_cols, grid_rows, auto_approve) VALUES (1, 'Lab', 8, 5, 0)")
        self.c.execute("INSERT INTO profiles (id, name, mode, network, agent, builtin) VALUES (1, 'Test', 'test', '{}', '{}', 1)")
        self.c.execute("INSERT INTO profiles (id, name, mode, network, agent, builtin) VALUES (2, 'Practice', 'practice', '{}', '{}', 1)")
        self.pc_n = 0
        self.seq = 0

    def session(self, sid: int, mode: str = "test", start_min: float = 0, length: float = 60) -> int:
        self.c.execute("INSERT INTO lab_sessions (id, lab_id, profile_id, course, mode, status, starts_at, ends_at, started_at, ended_at) "
                       "VALUES (?,1,?, 'CS101', ?, 'ended', ?, ?, ?, ?)",
                       (sid, 1 if mode == "test" else 2, mode, ts(start_min), ts(start_min + length), ts(start_min), ts(start_min + length)))
        return sid

    def pc(self, sid: int, name: str, caps: dict | None = None) -> int:
        self.pc_n += 1
        pid = self.pc_n
        if not self.c.execute("SELECT 1 FROM pcs WHERE id = ?", (pid,)).fetchone():
            self.c.execute("INSERT INTO pcs (id, lab_id, name, hostname, status, live_score, isolated, clean_shutdown, capabilities) "
                           "VALUES (?,1,?,?, 'clean', 0, 0, 0, ?)", (pid, name, name.lower(), json.dumps(caps or {"keys": True})))
        self.c.execute("INSERT INTO session_pcs (lab_session_id, pc_id) VALUES (?,?)", (sid, pid))
        return pid

    def student(self, sid: int, login: str, consent: bool = True, exempt: str | None = None) -> int:
        self.c.execute("INSERT INTO students (id, student_number, name, login, pseudonym, exempt, exempt_reason) VALUES (?,?,?,?,?,?,?)",
                       (sid, f"S{sid:04d}", f"Student {sid}", login, f"P-{sid:04d}", 1 if exempt else 0, exempt))
        self.c.execute("INSERT INTO consents (student_id, purpose, granted, form_version, recorded_at) VALUES (?, 'research', ?, '1.0', ?)",
                       (sid, 1 if consent else 0, ts(-100)))
        return sid

    def typing(self, sid, pc, user, start, minutes, kpm=180, bs=0.08, iki=110.0, bursts=1):
        for i in range(int(minutes * 12)):
            keys = int(kpm / 12)
            self.c.execute("INSERT INTO key_stats (pc_id, lab_session_id, user, window_start, window_s, keys, backspaces, iki_mean, "
                           "iki_median, bursts, paste_chords) VALUES (?,?,?,?,5,?,?,?,?,?,0)",
                           (pc, sid, user, ts(start + i / 12), keys, int(keys * bs), iki, iki, bursts))

    def paste(self, sid, pc, user, minute, chars, lines, code=True, copy=False):
        self.c.execute("INSERT INTO paste_events (pc_id, lab_session_id, user, is_copy, chars, lines, looks_like_code, code_score, "
                       "source_app, target_app, occurred_at) VALUES (?,?,?,?,?,?,?,?, 'firefox', 'code', ?)",
                       (pc, sid, user, 1 if copy else 0, chars, lines, 1 if code else 0, 0.9 if code else 0.1, ts(minute)))

    def alert(self, sid, pc, user, minute, rule, category, severity="high"):
        self.c.execute("INSERT INTO alerts (pc_id, lab_session_id, rule_id, severity, category, title, user, occurred_at) "
                       "VALUES (?,?,?,?,?,?,?,?)", (pc, sid, rule, severity, category, rule, user, ts(minute)))

    def blocked(self, sid, pc, minute, host="chatgpt.com"):
        self.c.execute("INSERT INTO net_events (pc_id, lab_session_id, client_ip, host, layer, action, category, occurred_at) "
                       "VALUES (?,?, '192.168.10.9', ?, 'sni', 'blocked', 'ai', ?)", (pc, sid, host, ts(minute)))

    def event(self, sid, pc, user, minute, type_, data):
        self.seq += 1
        self.c.execute("INSERT INTO events (pc_id, agent_id, seq, lab_session_id, type, occurred_at, received_at, user, data) "
                       "VALUES (?,?,?,?,?,?,?,?,?)", (pc, pc, self.seq, sid, type_, ts(minute), ts(minute), user, json.dumps(data)))


@pytest.fixture
def db():
    d = Db("sqlite:///:memory:")
    d.conn.executescript(SCHEMA)
    yield d
    d.conn.close()


@pytest.fixture
def lab(db):
    return LabBuilder(db)
