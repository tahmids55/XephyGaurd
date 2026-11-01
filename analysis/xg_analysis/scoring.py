"""Scoring.

v1: transparent weighted rule score, 0..100. Every point comes from a named signal with the value that
    triggered it, so a reviewer can trace and challenge each contribution.
v2 (optional, XG_SCORING_V2=1): Isolation Forest over the session cohort, shown *alongside* v1 with
    the features that deviate most; it never replaces the v1 explanation.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any

import numpy as np
import pandas as pd

GENERIC_NOT_OBSERVABLE = [
    "use of a phone or second device away from this PC",
    "answers memorised or written down before the session",
    "help from another person",
]


@dataclass
class Reason:
    signal: str
    label: str
    points: int
    value: Any = None

    def as_dict(self) -> dict[str, Any]:
        v = self.value
        if isinstance(v, (np.floating, float)):
            v = round(float(v), 3)
        elif isinstance(v, np.integer):
            v = int(v)
        return {"signal": self.signal, "label": self.label, "points": self.points, "value": v}


@dataclass
class ScoreResult:
    score: int
    confidence: str
    reasons: list[Reason]
    not_observable: list[str]
    features: dict[str, Any] = field(default_factory=dict)


def _z(value: float, mean: float, std: float) -> float | None:
    if value is None or mean is None or std is None or any(map(lambda x: isinstance(x, float) and math.isnan(x), (value, mean, std))):
        return None
    if std < 1e-6:
        return None
    return (value - mean) / std


def score_v1(f: dict[str, Any], *, mode: str, base: dict[str, tuple[float, float, int]] | None = None,
             caps: dict[str, bool] | None = None, exempt: str | None = None) -> ScoreResult:
    r: list[Reason] = []
    add = lambda sig, label, pts, val=None: r.append(Reason(sig, label, int(pts), val))  # noqa: E731

    # --- direct evidence (agent rules, network, tamper) ----------------------------------------
    if f["ai_flags"]:
        add("ai_flags", f"{f['ai_flags']} AI tool detection(s) on this PC", min(40, 25 * f["ai_flags"]), f["ai_flags"])
    if f["tamper_flags"]:
        add("tamper_flags", f"{f['tamper_flags']} tamper event(s) (agent stopped/killed, clock, config)", min(30, 20 * f["tamper_flags"]), f["tamper_flags"])
    if f.get("net_flags"):
        add("net_flags", "Tethering / VPN / second network route observed", min(25, 20 * f["net_flags"]), f["net_flags"])
    if f["blocked_attempts"]:
        add("blocked_attempts", f"{f['blocked_attempts']} blocked attempt(s) to reach AI or bypass services",
            min(15, 3 * f["blocked_attempts"]), f["blocked_attempts"])

    # --- behavioral signals (skipped for exempt students) --------------------------------------
    if not exempt:
        effective_paste = f["largest_paste"]
        if f["largest_paste"] >= 800:
            pts = 10 + (5 if f["largest_paste_code"] else 0)
            if f["early_paste_chars"] >= f["largest_paste"]:
                pts //= 2   # first minutes: often assignment boilerplate
                add("largest_paste", f"Large paste ({int(effective_paste)} chars) early in the session — may be assignment boilerplate", pts, effective_paste)
            else:
                add("largest_paste", f"Large paste of {int(effective_paste)} characters{' that looks like code' if f['largest_paste_code'] else ''}", pts, effective_paste)
        if f["paste_to_typed"] > 0.5 and f["pasted_chars"] >= 500:
            add("paste_to_typed", f"Pasted {f['paste_to_typed']:.1f}× as many characters as typed", 10, f["paste_to_typed"])
        if f["idle_then_burst"]:
            add("idle_then_burst", f"{f['idle_then_burst']} burst(s) of new code right after a {120 // 60}+ minute idle gap",
                min(16, 8 * f["idle_then_burst"]), f["idle_then_burst"])
        b = (base or {})
        used_baseline = False
        for feat, label in (("keys_per_min", "typing speed"), ("backspace_ratio", "correction rate"), ("burstiness", "typing burstiness")):
            if feat in b and b[feat][2] >= 2:
                z = _z(f.get(feat), b[feat][0], b[feat][1])
                used_baseline = True
                if z is not None and abs(z) >= 2.5:
                    add(f"z_{feat}", f"{label.capitalize()} differs strongly from this student's own baseline (z = {z:+.1f})", 8, z)
        if not used_baseline and f["keys_total"] >= 300 and not math.isnan(f.get("backspace_ratio", float("nan"))) \
                and f["backspace_ratio"] < 0.01:
            add("backspace_ratio", "Almost no corrections while typing (possible transcription); no personal baseline yet", 5,
                f["backspace_ratio"])
        if mode == "test" and f["time_outside_ide"] > 0.5:
            add("time_outside_ide", f"{f['time_outside_ide']:.0%} of focus time outside the IDE/terminal", 5, f["time_outside_ide"])
        if f["focus_switches_per_min"] > 4:
            add("focus_switches_per_min", f"Frequent window switching ({f['focus_switches_per_min']:.1f}/min)", 5, f["focus_switches_per_min"])

    r.sort(key=lambda x: -x.points)
    score = min(100, sum(x.points for x in r))

    # --- what could not be observed -------------------------------------------------------------
    caps = caps or {}
    missing = []
    if caps.get("keys") is False:
        missing.append("typing rhythm (key timing unavailable on this PC)")
    if caps.get("window") is False:
        missing.append("focused window (e.g. Wayland session)")
    if caps.get("clipboard") is False:
        missing.append("clipboard sizes (e.g. Wayland session)")
    if f["keys_total"] == 0 and caps.get("keys") is not False:
        missing.append("no typing recorded (student absent, or helper/agent not running)")
    if exempt:
        missing.append(f"behavioral signals ignored: student exempt ({exempt})")
    not_obs = missing + GENERIC_NOT_OBSERVABLE

    strong = {x.signal for x in r if x.points >= 10}
    families = {x.signal.split("_")[0] for x in r}
    confidence = "high" if len(strong) >= 2 or (len(strong) >= 1 and len(families) >= 3) else ("medium" if r else "low")
    if missing and confidence == "high":
        confidence = "medium"
    return ScoreResult(score, confidence, r, not_obs)


V2_FEATURES = ["keys_per_min", "backspace_ratio", "burstiness", "paste_to_typed", "largest_paste", "idle_then_burst",
               "focus_switches_per_min", "time_outside_ide", "blocked_attempts"]


def score_v2(features: pd.DataFrame, seed: int = 7) -> list[ScoreResult]:
    """Cohort anomaly score with Isolation Forest. Needs at least 6 students; otherwise returns []."""
    if len(features) < 6:
        return []
    from sklearn.ensemble import IsolationForest

    X = features[V2_FEATURES].astype(float).fillna(features[V2_FEATURES].astype(float).median()).fillna(0.0)
    mu, sd = X.mean(), X.std().replace(0, 1.0)
    Z = (X - mu) / sd
    model = IsolationForest(n_estimators=200, random_state=seed, contamination="auto").fit(Z)
    raw = -model.score_samples(Z)                     # higher = more anomalous
    lo, hi = float(np.min(raw)), float(np.max(raw))
    out = []
    for i, (_, row) in enumerate(Z.iterrows()):
        s = int(round(100 * (raw[i] - lo) / (hi - lo))) if hi > lo else 0
        top = row.abs().sort_values(ascending=False).head(3)
        reasons = [Reason(k, f"{k.replace('_', ' ')} is {row[k]:+.1f} SD from the session cohort", 0, float(row[k]))
                   for k in top.index if abs(row[k]) >= 1.0]
        out.append(ScoreResult(s, "low", reasons, ["v2 is an exploratory anomaly ranking; read the v1 reasons"]))
    return out
