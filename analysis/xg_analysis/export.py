"""Research exports: only students with a current research consent; identifiers replaced by pseudonyms."""
from __future__ import annotations

import json
import pathlib

import pandas as pd

from .db import Db, jload


def consenting(db: Db) -> pd.DataFrame:
    c = db.df("SELECT c.student_id, c.granted, c.recorded_at FROM consents c WHERE c.purpose = 'research'")
    if c.empty:
        return pd.DataFrame(columns=["student_id"])
    latest = c.sort_values("recorded_at").groupby("student_id").tail(1)
    return latest[latest.granted.astype(bool)][["student_id"]]


def research_export(db: Db, out: pathlib.Path, fmt: str = "csv", since: str | None = None, until: str | None = None) -> list[pathlib.Path]:
    out.mkdir(parents=True, exist_ok=True)
    ok = consenting(db)
    students = db.df("SELECT id AS student_id, login, pseudonym FROM students")
    students = students.merge(ok, on="student_id")
    where, params = "WHERE 1=1", []
    if since:
        where += " AND s.starts_at >= ?"
        params.append(since)
    if until:
        where += " AND s.starts_at <= ?"
        params.append(until)
    scores = db.df("SELECT sc.lab_session_id, sc.student_id, sc.method, sc.score, sc.confidence, sc.features, sc.reasons, "
                   f"s.mode, s.course, s.experiment_condition, s.experiment_group FROM scores sc JOIN lab_sessions s ON s.id = sc.lab_session_id {where}",
                   tuple(params))
    scores = scores.merge(students[["student_id", "pseudonym"]], on="student_id")
    feats = pd.json_normalize([jload(f) for f in scores.features]) if not scores.empty else pd.DataFrame()
    table = pd.concat([scores.drop(columns=["features", "reasons", "student_id"]).reset_index(drop=True), feats], axis=1)
    table["reason_signals"] = [";".join(r["signal"] for r in jload(x)) for x in scores.reasons] if not scores.empty else []
    files = []
    p = out / f"scores.{fmt}"
    table.to_parquet(p) if fmt == "parquet" else table.to_csv(p, index=False)
    files.append(p)
    (out / "README.json").write_text(json.dumps({
        "participants": int(students.shape[0]), "rows": int(table.shape[0]),
        "note": "Only students with a current research consent are included. 'pseudonym' is a stable random id; "
                "no names, logins or student numbers are exported.",
    }, indent=1))
    files.append(out / "README.json")
    return files
