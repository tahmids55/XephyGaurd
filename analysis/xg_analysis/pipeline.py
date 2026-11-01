"""Score a session end-to-end and write the `scores` table."""
from __future__ import annotations

import json
import math
import os
from datetime import datetime, timezone
from typing import Any

import pandas as pd

from .db import Db
from .features import FEATURES, baseline, compute, load_session
from .scoring import score_v1, score_v2


def _clean(v: Any) -> Any:
    if isinstance(v, float) and math.isnan(v):
        return None
    if hasattr(v, "item"):
        return v.item()
    return v


def score_session(db: Db, session_id: int, v2: bool | None = None) -> dict[str, Any]:
    v2 = (os.environ.get("XG_SCORING_V2", "0") == "1") if v2 is None else v2
    sd = load_session(db, session_id)
    feats = compute(sd)
    if feats.empty:
        return {"session_id": session_id, "scored": 0, "flagged": []}
    students = sd.students.set_index("login") if not sd.students.empty else pd.DataFrame()
    base = baseline(db, [u for u in feats.user.unique() if u], str(sd.session["starts_at"]), int(sd.session["lab_id"]))
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    rows = []
    for _, f in feats.iterrows():
        login = f.user.split("\\")[-1] if f.user else ""
        st = students.loc[login] if login and not students.empty and login in students.index else None
        b = {}
        if not base.empty and f.user in base.index:
            for feat in ("keys_per_min", "backspace_ratio", "burstiness"):
                b[feat] = (base.loc[f.user, (feat, "mean")], base.loc[f.user, (feat, "std")], int(base.loc[f.user, (feat, "count")]))
        exempt = (st.exempt_reason or "accommodation") if st is not None and bool(st.exempt) else None
        res = score_v1(f.to_dict(), mode=sd.session["mode"], base=b, caps=sd.caps.get(int(f.pc_id), {}), exempt=exempt)
        rows.append(("v1", f, st, res))
    if v2:
        for (_, f), res in zip(feats.iterrows(), score_v2(feats)):
            login = f.user.split("\\")[-1] if f.user else ""
            st = students.loc[login] if login and not students.empty and login in students.index else None
            rows.append(("v2", f, st, res))

    with db.transaction():
        db.execute("DELETE FROM scores WHERE lab_session_id = ?", (session_id,))
        for method, f, st, res in rows:
            feat_json = {k: _clean(f[k]) for k in FEATURES if k in f}
            db.execute(
                "INSERT INTO scores (lab_session_id, pc_id, student_id, `user`, method, score, confidence, reasons, features, "
                "not_observable, created_at, updated_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
                (session_id, int(f.pc_id), int(st.student_id) if st is not None else None, f.user or None, method,
                 int(res.score), res.confidence, json.dumps([r.as_dict() for r in res.reasons]), json.dumps(feat_json),
                 json.dumps(res.not_observable), now, now))
    flagged = [{"pc_id": int(f.pc_id), "user": f.user, "score": res.score, "confidence": res.confidence}
               for m, f, _, res in rows if m == "v1" and res.score >= 30]
    return {"session_id": session_id, "scored": sum(1 for r in rows if r[0] == "v1"), "flagged": flagged}
