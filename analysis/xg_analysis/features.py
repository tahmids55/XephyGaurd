"""Per-student, per-session behavioral features (spec section 8).

All inputs are numeric aggregates produced on the PCs (no content). One row per (pc_id, user).
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

import numpy as np
import pandas as pd

from .db import Db, jload

IDE_APPS = {"code", "code-oss", "codium", "vscodium", "idea", "idea64", "pycharm", "pycharm64", "clion", "clion64",
            "webstorm", "eclipse", "netbeans", "devenv", "vim", "nvim", "gvim", "emacs", "gedit", "kate", "geany",
            "notepad++", "sublime_text", "rstudio", "spyder", "jupyter-lab", "thonny", "bluej", "codeblocks",
            "gnome-terminal-server", "konsole", "xterm", "windowsterminal", "cmd", "powershell", "pwsh", "java", "javaw"}
AI_CATEGORIES = {"local_llm", "ai_editor", "ai_desktop", "cli_agent", "extension", "local_model"}
IDLE_GAP_S = 120          # "idle" = no keys for 2 minutes ...
BURST_WINDOW_S = 60       # ... followed within 1 minute by
BURST_PASTE_CHARS = 300   # a paste of 300+ chars
BURST_GROWTH_BYTES = 1500  # or 1.5 kB+ growth of the work folder

FEATURES = ["keys_total", "active_minutes", "keys_per_min", "iki_median", "burstiness", "backspace_ratio",
            "pasted_chars", "paste_count", "largest_paste", "largest_paste_code", "paste_to_typed",
            "idle_then_burst", "focus_switches_per_min", "time_outside_ide", "blocked_attempts",
            "adapter_device_changes", "ai_flags", "tamper_flags", "early_paste_chars"]


@dataclass
class SessionData:
    session: dict[str, Any]
    pcs: pd.DataFrame
    key_stats: pd.DataFrame
    pastes: pd.DataFrame
    alerts: pd.DataFrame
    net: pd.DataFrame
    events: pd.DataFrame
    students: pd.DataFrame
    caps: dict[int, dict[str, bool]] = field(default_factory=dict)


def _ts(s: pd.Series) -> pd.Series:
    return pd.to_datetime(s, utc=True, format="mixed")


def load_session(db: Db, session_id: int) -> SessionData:
    s = db.one("SELECT * FROM lab_sessions WHERE id = ?", (session_id,))
    if s is None:
        raise KeyError(f"session {session_id} not found")
    q = lambda sql: db.df(sql, (session_id,))  # noqa: E731
    pcs = db.df("SELECT p.id AS pc_id, p.name, p.capabilities, sp.student_id FROM session_pcs sp "
                "JOIN pcs p ON p.id = sp.pc_id WHERE sp.lab_session_id = ?", (session_id,))
    ks = q("SELECT pc_id, `user`, window_start, window_s, `keys`, backspaces, iki_median, iki_mean, bursts, paste_chords "
           "FROM key_stats WHERE lab_session_id = ?")
    pa = q("SELECT pc_id, `user`, is_copy, chars, `lines`, looks_like_code, source_app, target_app, occurred_at "
           "FROM paste_events WHERE lab_session_id = ?")
    al = q("SELECT pc_id, `user`, rule_id, severity, category, title, occurred_at FROM alerts WHERE lab_session_id = ?")
    ne = q("SELECT pc_id, host, action, category, occurred_at FROM net_events WHERE lab_session_id = ?")
    ev = q("SELECT pc_id, `user`, type, occurred_at, data FROM events WHERE lab_session_id = ? "
           "AND type IN ('window_focus','file_activity','net_adapter','device','helper_status')")
    st = db.df("SELECT id AS student_id, login, name, pseudonym, exempt, exempt_reason FROM students")
    for df, col in ((ks, "window_start"), (pa, "occurred_at"), (al, "occurred_at"), (ne, "occurred_at"), (ev, "occurred_at")):
        if not df.empty:
            df[col] = _ts(df[col])
    caps: dict[int, dict[str, bool]] = {}
    for _, r in pcs.iterrows():
        caps[int(r.pc_id)] = {k: bool(v) for k, v in jload(r.capabilities).items()}
    for _, r in ev[ev.type == "helper_status"].iterrows() if not ev.empty else []:
        d = jload(r.data)
        for k, v in (d.get("caps") or {}).items():
            caps.setdefault(int(r.pc_id), {})[k] = bool(v)
    return SessionData(s, pcs, ks, pa, al, ne, ev, st, caps)


def _users(sd: SessionData) -> list[tuple[int, str]]:
    pairs: set[tuple[int, str]] = set()
    for df in (sd.key_stats, sd.pastes, sd.alerts, sd.events):
        if not df.empty and "user" in df:
            for pc, u in df[["pc_id", "user"]].dropna().itertuples(index=False):
                if u:
                    pairs.add((int(pc), str(u)))
    # PCs with only network events or no user attribution still get a row.
    known_pcs = {p for p, _ in pairs}
    for pc in sd.pcs.pc_id.tolist():
        if int(pc) not in known_pcs and (not sd.alerts.empty and (sd.alerts.pc_id == pc).any()
                                         or not sd.net.empty and (sd.net.pc_id == pc).any()):
            pairs.add((int(pc), ""))
    return sorted(pairs)


def _focus_stats(ev: pd.DataFrame, end: pd.Timestamp) -> tuple[float, float]:
    f = ev[ev.type == "window_focus"].sort_values("occurred_at")
    if f.empty:
        return 0.0, 0.0
    apps = [str(jload(d).get("app", "")).lower() for d in f.data]
    times = list(f.occurred_at) + [end]
    total = outside = 0.0
    for i, app in enumerate(apps):
        dur = max(0.0, (times[i + 1] - times[i]).total_seconds())
        total += dur
        if app not in IDE_APPS:
            outside += dur
    minutes = max(1.0, (times[-1] - times[0]).total_seconds() / 60)
    return len(apps) / minutes, (outside / total if total else 0.0)


def _idle_then_burst(ks: pd.DataFrame, pa: pd.DataFrame, files: pd.DataFrame) -> int:
    """Counts bursts (big paste or work-folder growth) that follow an idle period without typing."""
    if ks.empty:
        key_times = []
    else:
        key_times = sorted(ks[ks["keys"] > 0].window_start.tolist())
    bursts: list[pd.Timestamp] = []
    if not pa.empty:
        bursts += pa[(~pa.is_copy.astype(bool)) & (pa.chars >= BURST_PASTE_CHARS)].occurred_at.tolist()
    if not files.empty:
        for t, d in zip(files.occurred_at, files.data):
            if int(jload(d).get("bytes_delta", 0)) >= BURST_GROWTH_BYTES:
                bursts.append(t)
    n = 0
    for b in sorted(bursts):
        prior = [t for t in key_times if t <= b]
        last_key = prior[-1] if prior else None
        if last_key is None or (b - last_key).total_seconds() >= IDLE_GAP_S:
            n += 1
    return n


def compute(sd: SessionData) -> pd.DataFrame:
    start = _ts(pd.Series([sd.session["started_at"] or sd.session["starts_at"]]))[0]
    end_raw = sd.session.get("ended_at") or sd.session.get("ends_at")
    end = _ts(pd.Series([end_raw]))[0] if end_raw else pd.Timestamp.now(tz="UTC")
    rows = []
    for pc, user in _users(sd):
        sel = lambda df: df[(df.pc_id == pc) & ((df.user == user) if user and "user" in df else True)] if not df.empty else df  # noqa: E731
        ks, pa, al, ev = sel(sd.key_stats), sel(sd.pastes), sel(sd.alerts), sel(sd.events)
        ne = sd.net[sd.net.pc_id == pc] if not sd.net.empty else sd.net
        keys_total = float(ks["keys"].sum()) if not ks.empty else 0.0
        active = ks[ks["keys"] > 0] if not ks.empty else ks
        active_minutes = float((active.window_s.sum() / 60.0)) if not active.empty else 0.0
        pastes = pa[~pa.is_copy.astype(bool)] if not pa.empty else pa
        pasted = float(pastes.chars.sum()) if not pastes.empty else 0.0
        largest = pastes.loc[pastes.chars.idxmax()] if not pastes.empty else None
        early = pastes[pastes.occurred_at <= start + pd.Timedelta(minutes=5)] if not pastes.empty else pastes
        fsw, outside = _focus_stats(ev, end) if not ev.empty else (0.0, 0.0)
        files = ev[ev.type == "file_activity"] if not ev.empty else ev
        cats = set(al.category.dropna()) if not al.empty else set()
        rows.append({
            "pc_id": pc, "user": user,
            "keys_total": keys_total,
            "active_minutes": active_minutes,
            "keys_per_min": keys_total / active_minutes if active_minutes else 0.0,
            "iki_median": float(active.iki_median.median()) if not active.empty and active.iki_median.notna().any() else np.nan,
            "burstiness": float(active.bursts.sum() / len(active)) if not active.empty else 0.0,
            "backspace_ratio": float(ks.backspaces.sum() / keys_total) if keys_total else np.nan,
            "pasted_chars": pasted,
            "paste_count": int(len(pastes)),
            "largest_paste": float(largest.chars) if largest is not None else 0.0,
            "largest_paste_code": bool(largest.looks_like_code) if largest is not None else False,
            "paste_to_typed": pasted / max(keys_total, 1.0),
            "idle_then_burst": _idle_then_burst(ks, pa, files),
            "focus_switches_per_min": fsw,
            "time_outside_ide": outside,
            "blocked_attempts": int((ne.action == "blocked").sum()) if not ne.empty else 0,
            "adapter_device_changes": int(((ev.type == "net_adapter") | (ev.type == "device")).sum()) if not ev.empty else 0,
            "ai_flags": int(al.category.isin(AI_CATEGORIES).sum()) if not al.empty else 0,
            "tamper_flags": int((al.category == "tamper").sum()) if not al.empty else 0,
            "net_flags": int(al.rule_id.str.startswith("net.").sum()) if not al.empty else 0,
            "early_paste_chars": float(early.chars.sum()) if not early.empty else 0.0,
            "alert_titles": sorted(set(al.title)) if not al.empty else [],
            "categories": sorted(cats),
        })
    return pd.DataFrame(rows)


def baseline(db: Db, logins: list[str], before: str, lab_id: int) -> pd.DataFrame:
    """Per-student mean/std of key features across earlier Practice sessions (the student's own baseline)."""
    if not logins:
        return pd.DataFrame()
    sessions = db.df("SELECT id FROM lab_sessions WHERE lab_id = ? AND mode = 'practice' AND status = 'ended' "
                     "AND starts_at < ? ORDER BY starts_at DESC LIMIT 20", (lab_id, before))
    frames = []
    for sid in sessions.id.tolist():
        f = compute(load_session(db, int(sid)))
        if not f.empty:
            frames.append(f[f.user.isin(logins)])
    if not frames:
        return pd.DataFrame()
    allf = pd.concat(frames)
    cols = ["keys_per_min", "backspace_ratio", "paste_to_typed", "iki_median", "burstiness"]
    agg = allf.groupby("user")[cols].agg(["mean", "std", "count"])
    return agg
