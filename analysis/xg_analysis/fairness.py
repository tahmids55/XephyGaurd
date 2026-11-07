"""Fairness auditing and rule precision gating for XephyGuard pilot evaluations.

Designed to run with zero third-party dependencies (pure Python stdlib) while
also accepting pandas DataFrame inputs when available.
"""
from __future__ import annotations

from typing import Any, Iterable

NON_PUNITIVE_AI_POLICIES = frozenset({"permitted_with_disclosure", "open"})


def _as_dicts(records: Iterable[dict[str, Any]] | Any) -> list[dict[str, Any]]:
    if hasattr(records, "to_dict"):
        try:
            return list(records.to_dict(orient="records"))
        except TypeError:
            pass
    return [dict(r) for r in records]


def effective_score(record: dict[str, Any]) -> float:
    """Return the effective suspicion score for a student session record.

    Students with an accommodation exemption (`exempt=True`) or sessions run
    under `ai_policy in ('permitted_with_disclosure', 'open')` always receive
    an effective punitive score of `0.0`.
    """
    if bool(record.get("exempt")):
        return 0.0
    ai_policy = str(record.get("ai_policy") or "forbidden").strip().lower()
    if ai_policy in NON_PUNITIVE_AI_POLICIES:
        return 0.0
    raw = record.get("score", 0.0)
    return float(0.0 if raw is None else raw)


def _is_honest(record: dict[str, Any]) -> bool:
    if "ground_truth_honest" in record and record["ground_truth_honest"] is not None:
        return bool(record["ground_truth_honest"])
    if "label" in record and record["label"] is not None:
        return int(record["label"]) == 0
    return True


def audit_fairness(
    records: Iterable[dict[str, Any]] | Any,
    score_threshold: float = 50.0,
    min_rule_precision: float = 0.70,
) -> dict[str, Any]:
    """Compute per-cohort fairness metrics, disparate impact ratio, and rule precision gates.

    Args:
        records: Iterable of session dicts with keys:
            - `student_id`: str | int (optional)
            - `group`: cohort name, e.g. `"native_typist"`, `"non_native_typist"`, `"accommodated"`
            - `ground_truth_honest`: bool (`True` for honest session, `False` for red-team / AI use)
            - `score`: float suspicion score
            - `exempt`: bool accommodation exemption flag
            - `ai_policy`: `"forbidden" | "permitted_with_disclosure" | "open"`
            - `rule_hits`: optional iterable of rule IDs triggered in the session
        score_threshold: Suspicion score threshold for raising a high-severity flag.
        min_rule_precision: Minimum acceptable precision `[0, 1]` on labeled sessions before a
            rule is flagged for demotion from high-severity / kill.
    """
    rows = _as_dicts(records)
    buckets: dict[str, list[dict[str, Any]]] = {}
    rule_stats: dict[str, dict[str, int]] = {}

    for row in rows:
        grp = str(row.get("group") or "default")
        buckets.setdefault(grp, []).append(row)

        honest = _is_honest(row)
        exempt_or_open = bool(row.get("exempt")) or (
            str(row.get("ai_policy") or "forbidden").strip().lower() in NON_PUNITIVE_AI_POLICIES
        )
        if exempt_or_open:
            continue

        hits = row.get("rule_hits") or []
        if isinstance(hits, str):
            hits = [h.strip() for h in hits.split(",") if h.strip()]
        for rule_id in set(hits):
            st = rule_stats.setdefault(str(rule_id), {"fires": 0, "true_positives": 0, "false_positives": 0})
            st["fires"] += 1
            if honest:
                st["false_positives"] += 1
            else:
                st["true_positives"] += 1

    group_metrics: dict[str, dict[str, Any]] = {}
    honest_fprs: list[float] = []
    flag_rates: list[float] = []

    for grp in sorted(buckets.keys()):
        g_rows = buckets[grp]
        count = len(g_rows)
        honest_count = 0
        non_honest_count = 0
        flagged_total = 0
        flagged_honest = 0
        true_positives = 0

        for r in g_rows:
            honest = _is_honest(r)
            eff = effective_score(r)
            flagged = eff >= score_threshold
            if flagged:
                flagged_total += 1
            if honest:
                honest_count += 1
                if flagged:
                    flagged_honest += 1
            else:
                non_honest_count += 1
                if flagged:
                    true_positives += 1

        fpr = round(flagged_honest / honest_count, 4) if honest_count > 0 else 0.0
        tpr = round(true_positives / non_honest_count, 4) if non_honest_count > 0 else 0.0
        flag_rate = round(flagged_total / count, 4) if count > 0 else 0.0

        if honest_count > 0:
            honest_fprs.append(fpr)
        if count > 0:
            flag_rates.append(flag_rate)

        group_metrics[grp] = {
            "count": count,
            "honest_count": honest_count,
            "flagged_honest": flagged_honest,
            "false_positive_rate": fpr,
            "true_positive_rate": tpr,
            "flag_rate": flag_rate,
        }

    max_fpr_gap = round(max(honest_fprs) - min(honest_fprs), 4) if len(honest_fprs) >= 2 else 0.0
    non_zero_flags = [fr for fr in flag_rates if fr > 0.0]
    max_flag_rate = max(flag_rates) if flag_rates else 0.0
    disparate_impact_ratio = (
        round(min(non_zero_flags) / max_flag_rate, 4) if max_flag_rate > 0.0 and non_zero_flags else 1.0
    )

    passes_fairness_gate = bool(
        max_fpr_gap <= 0.05
        and all(m["false_positive_rate"] <= 0.02 for m in group_metrics.values() if m["honest_count"] > 0)
    )

    rule_precision: dict[str, dict[str, Any]] = {}
    low_precision_rules: list[str] = []
    for rid in sorted(rule_stats.keys()):
        st = rule_stats[rid]
        fires = st["fires"]
        prec = round(st["true_positives"] / fires, 4) if fires > 0 else 1.0
        rule_precision[rid] = {
            "fires": fires,
            "true_positives": st["true_positives"],
            "false_positives": st["false_positives"],
            "precision": prec,
        }
        if prec < min_rule_precision:
            low_precision_rules.append(rid)

    return {
        "total_sessions": len(rows),
        "score_threshold": score_threshold,
        "min_rule_precision": min_rule_precision,
        "groups": group_metrics,
        "max_fpr_gap": max_fpr_gap,
        "disparate_impact_ratio": disparate_impact_ratio,
        "passes_fairness_gate": passes_fairness_gate,
        "rule_precision": rule_precision,
        "low_precision_rules": low_precision_rules,
    }
