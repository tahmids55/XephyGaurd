import json

import pytest

from xg_analysis.export import research_export
from xg_analysis.features import compute, load_session
from xg_analysis.pipeline import score_session
from xg_analysis.scoring import score_v1


def honest_and_suspicious(lab, sid=10):
    lab.session(sid, "test")
    a = lab.pc(sid, "A1")
    b = lab.pc(sid, "A2")
    lab.student(1, "alice")
    lab.student(2, "bob")
    # alice: steady typing with corrections the whole session, small pastes, stays in the IDE
    lab.typing(sid, a, "alice", 1, 55, kpm=160, bs=0.09)
    lab.paste(sid, a, "alice", 20, 40, 2)
    lab.event(sid, a, "alice", 0.5, "window_focus", {"app": "code"})
    # bob: types a little, goes idle, then a large code paste appears; AI tool + blocked attempts
    lab.typing(sid, b, "bob", 1, 10, kpm=120, bs=0.05)
    lab.event(sid, b, "bob", 0.5, "window_focus", {"app": "code"})
    lab.event(sid, b, "bob", 12, "window_focus", {"app": "firefox"})
    lab.event(sid, b, "bob", 40, "window_focus", {"app": "code"})
    lab.paste(sid, b, "bob", 40, 2400, 85, code=True)
    lab.event(sid, b, "bob", 41, "file_activity", {"dir": "/home/bob/lab", "saves": 2, "bytes_delta": 2600})
    lab.alert(sid, b, "bob", 15, "proc.ollama", "local_llm")
    for m in (13, 14, 16):
        lab.blocked(sid, b, m)
    lab.db.conn.commit()
    return a, b


def test_features(lab, db):
    a, b = honest_and_suspicious(lab)
    f = compute(load_session(db, 10)).set_index("user")
    assert f.loc["alice", "keys_per_min"] == pytest.approx(156, rel=0.05)   # 13 keys per 5 s window
    assert f.loc["bob", "largest_paste"] == 2400
    assert f.loc["bob", "idle_then_burst"] >= 1
    assert f.loc["alice", "idle_then_burst"] == 0
    assert f.loc["bob", "blocked_attempts"] == 3
    assert f.loc["bob", "ai_flags"] == 1
    assert f.loc["bob", "time_outside_ide"] > 0.4
    assert f.loc["bob", "paste_to_typed"] > 1


def test_v1_scores_are_explained_and_ranked(lab, db):
    honest_and_suspicious(lab)
    out = score_session(db, 10, v2=False)
    assert out["scored"] == 2
    rows = {r[0]: r for r in db.conn.execute("SELECT user, score, confidence, reasons, not_observable FROM scores")}
    bob, alice = rows["bob"], rows["alice"]
    assert bob[1] >= 60 and alice[1] <= 10
    assert bob[2] == "high"
    signals = [r["signal"] for r in json.loads(bob[3])]
    assert {"ai_flags", "idle_then_burst", "largest_paste", "blocked_attempts"} <= set(signals)
    assert sum(r["points"] for r in json.loads(bob[3])) >= bob[1]          # every point is traceable
    assert any("phone" in x for x in json.loads(alice[4]))                  # states what is not observable
    assert [f["user"] for f in out["flagged"]] == ["bob"]
    # idempotent re-scoring
    score_session(db, 10, v2=False)
    assert db.conn.execute("SELECT count(*) FROM scores").fetchone()[0] == 2


def test_exempt_student_behavior_signals_ignored(lab, db):
    lab.session(11, "test")
    pc = lab.pc(11, "B1")
    lab.student(3, "carol", exempt="speech-to-text tool")
    lab.typing(11, pc, "carol", 1, 5, kpm=60, bs=0.0)
    lab.paste(11, pc, "carol", 30, 3000, 100)
    lab.db.conn.commit()
    score_session(db, 11, v2=False)
    score, reasons, nobs = db.conn.execute("SELECT score, reasons, not_observable FROM scores").fetchone()
    assert score == 0
    assert any("exempt" in x for x in json.loads(nobs))


def test_early_boilerplate_paste_discounted():
    base = dict(ai_flags=0, tamper_flags=0, net_flags=0, blocked_attempts=0, largest_paste=1200, largest_paste_code=True,
                early_paste_chars=1200, paste_to_typed=0.2, pasted_chars=1200, idle_then_burst=0, keys_total=2000,
                backspace_ratio=0.08, time_outside_ide=0.1, focus_switches_per_min=1, keys_per_min=150, burstiness=1)
    early = score_v1(base, mode="test")
    late = score_v1({**base, "early_paste_chars": 0}, mode="test")
    assert early.score < late.score
    assert "boilerplate" in early.reasons[0].label


def test_personal_baseline_z_scores():
    f = dict(ai_flags=0, tamper_flags=0, net_flags=0, blocked_attempts=0, largest_paste=0, largest_paste_code=False,
             early_paste_chars=0, paste_to_typed=0, pasted_chars=0, idle_then_burst=0, keys_total=3000,
             backspace_ratio=0.002, time_outside_ide=0, focus_switches_per_min=0, keys_per_min=420, burstiness=1)
    res = score_v1(f, mode="test", base={"keys_per_min": (150.0, 30.0, 6), "backspace_ratio": (0.08, 0.02, 6)})
    sig = {r.signal for r in res.reasons}
    assert {"z_keys_per_min", "z_backspace_ratio"} <= sig
    # slow typist relative to the cohort but normal for themself: no flag
    calm = score_v1({**f, "keys_per_min": 60, "backspace_ratio": 0.1}, mode="test", base={"keys_per_min": (55.0, 10.0, 6)})
    assert calm.score == 0


def test_v2_isolation_forest_alongside_v1(lab, db):
    lab.session(12, "test")
    for i in range(8):
        pc = lab.pc(12, f"C{i}")
        user = f"u{i}"
        lab.student(100 + i, user)
        lab.typing(12, pc, user, 1, 40, kpm=150 + i * 3, bs=0.07)
    odd = lab.pc(12, "C9")
    lab.student(200, "odd")
    lab.typing(12, odd, "odd", 1, 3, kpm=400, bs=0.0)
    lab.paste(12, odd, "odd", 30, 4000, 120)
    lab.db.conn.commit()
    score_session(db, 12, v2=True)
    v2 = dict(db.conn.execute("SELECT user, score FROM scores WHERE method = 'v2'").fetchall())
    assert len(v2) == 9
    assert max(v2, key=v2.get) == "odd"
    assert db.conn.execute("SELECT count(*) FROM scores WHERE method = 'v1'").fetchone()[0] == 9


def test_research_export_only_consenting_and_pseudonymized(lab, db, tmp_path):
    honest_and_suspicious(lab)
    db.conn.execute("INSERT INTO consents (student_id, purpose, granted, form_version, recorded_at) VALUES (2, 'research', 0, '1.0', '2026-12-01 00:00:00')")
    db.conn.commit()
    score_session(db, 10, v2=False)
    files = research_export(db, tmp_path, "csv")
    text = files[0].read_text()
    assert "P-0001" in text and "P-0002" not in text        # bob withdrew consent
    assert "alice" not in text and "Student" not in text
