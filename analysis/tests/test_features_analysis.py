"""Characterization tests: pin CURRENT behavior (docs/features/analysis). Not a spec."""
import json
import threading
from http.server import ThreadingHTTPServer
from urllib import error, request

import pytest

from xg_analysis import service
from xg_analysis.scoring import GENERIC_NOT_OBSERVABLE, score_v1

ZERO = dict(ai_flags=0, tamper_flags=0, net_flags=0, blocked_attempts=0, largest_paste=0, largest_paste_code=False,
            early_paste_chars=0, paste_to_typed=0, pasted_chars=0, idle_then_burst=0, keys_total=1000,
            backspace_ratio=0.08, time_outside_ide=0, focus_switches_per_min=0, keys_per_min=150, burstiness=1)


# ------------------------------------------------------------------ F009 HTTP service
class _FakeDb:
    ok = True

    def ping(self):
        return self.ok


@pytest.fixture
def srv(monkeypatch):
    monkeypatch.setenv("XG_INTERNAL_TOKEN", "tok")
    monkeypatch.setattr(service, "Db", _FakeDb)
    calls = []

    def fake_score(db, sid):
        calls.append(sid)
        if sid == 404:
            raise KeyError("no session")
        if sid == 500:
            raise RuntimeError("boom")
        return {"session_id": sid, "scored": 1, "flagged": []}

    monkeypatch.setattr(service, "score_session", fake_score)
    httpd = ThreadingHTTPServer(("127.0.0.1", 0), service.Handler)
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    yield f"http://127.0.0.1:{httpd.server_address[1]}", calls
    httpd.shutdown()


def _call(url, method="GET", body=None, token=None):
    req = request.Request(url, method=method, data=None if body is None else (body if isinstance(body, bytes) else json.dumps(body).encode()))
    if token is not None:
        req.add_header("Authorization", f"Bearer {token}")
    try:
        with request.urlopen(req, timeout=5) as r:
            return r.status, json.loads(r.read())
    except error.HTTPError as e:
        return e.code, json.loads(e.read())


def test_f009_health_needs_no_token_and_reflects_db(srv):
    url, _ = srv
    assert _call(url + "/health") == (200, {"ok": True})
    _FakeDb.ok = False
    try:
        assert _call(url + "/health") == (503, {"ok": False})
    finally:
        _FakeDb.ok = True
    assert _call(url + "/nope")[0] == 404


def test_f009_score_requires_exact_bearer_token(srv):
    url, calls = srv
    assert _call(url + "/score", "POST", {"session_id": 1})[0] == 401
    assert _call(url + "/score", "POST", {"session_id": 1}, token="wrong")[0] == 401
    assert calls == []
    assert _call(url + "/score", "POST", {"session_id": 7}, token="tok") == (200, {"session_id": 7, "scored": 1, "flagged": []})
    assert calls == [7]


def test_f009_score_error_mapping(srv, monkeypatch):
    url, _ = srv
    assert _call(url + "/score", "POST", {}, token="tok")[0] == 404                      # missing session_id -> KeyError
    assert _call(url + "/score", "POST", {"session_id": 404}, token="tok")[0] == 404      # unknown session
    assert _call(url + "/score", "POST", {"session_id": "abc"}, token="tok")[0] == 422
    assert _call(url + "/score", "POST", b"{not json", token="tok")[0] == 422
    code, body = _call(url + "/score", "POST", {"session_id": 500}, token="tok")
    assert code == 500 and body == {"error": "boom"}
    assert _call(url + "/other", "POST", {"session_id": 1}, token="tok")[0] == 404


def test_f009_empty_server_token_rejects_everything(srv, monkeypatch):
    url, _ = srv
    monkeypatch.setenv("XG_INTERNAL_TOKEN", "")
    assert _call(url + "/score", "POST", {"session_id": 1}, token="")[0] == 401


# ------------------------------------------------------------------ F048 scoring
def test_f048_clean_session_scores_zero_with_no_reasons_and_low_confidence():
    r = score_v1(ZERO, mode="test")
    assert (r.score, r.confidence, r.reasons) == (0, "low", [])
    assert r.not_observable == GENERIC_NOT_OBSERVABLE


def test_f048_every_reason_is_traceable_and_score_is_capped_at_100():
    f = {**ZERO, "ai_flags": 5, "tamper_flags": 5, "net_flags": 5, "blocked_attempts": 50, "largest_paste": 5000,
         "largest_paste_code": True, "paste_to_typed": 3, "pasted_chars": 5000, "idle_then_burst": 5,
         "time_outside_ide": 0.9, "focus_switches_per_min": 9}
    r = score_v1(f, mode="test")
    assert r.score == 100
    assert sum(x.points for x in r.reasons) > 100            # the cap hides surplus points; reasons still show all
    assert [x.points for x in r.reasons] == sorted((x.points for x in r.reasons), reverse=True)
    for x in r.reasons:
        d = x.as_dict()
        assert set(d) == {"signal", "label", "points", "value"} and d["label"]
    assert "penalty" not in json.dumps([x.as_dict() for x in r.reasons]).lower()


def test_f048_signal_point_caps():
    assert score_v1({**ZERO, "ai_flags": 1}, mode="test").score == 25
    assert score_v1({**ZERO, "ai_flags": 9}, mode="test").score == 40
    assert score_v1({**ZERO, "tamper_flags": 9}, mode="test").score == 30
    assert score_v1({**ZERO, "blocked_attempts": 9}, mode="test").score == 15
    assert score_v1({**ZERO, "net_flags": 1}, mode="test").score == 20


def test_f048_ide_time_only_counts_in_test_mode_and_confidence_rules():
    f = {**ZERO, "time_outside_ide": 0.8}
    assert score_v1(f, mode="test").score == 5
    assert score_v1(f, mode="practice").score == 0
    two_strong = score_v1({**ZERO, "ai_flags": 1, "blocked_attempts": 5}, mode="test")
    assert two_strong.confidence == "high"
    assert score_v1({**ZERO, "ai_flags": 1}, mode="test").confidence == "medium"   # one strong family only
    one = score_v1({**ZERO, "blocked_attempts": 1}, mode="test")
    assert one.confidence == "medium"


def test_f048_unobservable_capabilities_are_reported_and_cap_confidence_at_medium():
    f = {**ZERO, "ai_flags": 1, "blocked_attempts": 5}
    r = score_v1(f, mode="test", caps={"keys": False, "window": False, "clipboard": False})
    assert r.confidence == "medium"
    assert any("typing rhythm" in x for x in r.not_observable)
    assert any("focused window" in x for x in r.not_observable)
    assert any("clipboard" in x for x in r.not_observable)
    absent = score_v1({**ZERO, "keys_total": 0}, mode="test")
    assert any("no typing recorded" in x for x in absent.not_observable)


def test_f048_exempt_student_keeps_direct_evidence_but_loses_behavioral_points():
    f = {**ZERO, "ai_flags": 1, "largest_paste": 3000, "largest_paste_code": True}
    assert score_v1(f, mode="test").score == 40
    ex = score_v1(f, mode="test", exempt="accessibility tool")
    assert ex.score == 25
    assert any("exempt (accessibility tool)" in x for x in ex.not_observable)
