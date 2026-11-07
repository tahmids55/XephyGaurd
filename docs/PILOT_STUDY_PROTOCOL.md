# XephyGuard Pre-Registered Pilot Study Protocol & Fairness Audit

This document specifies the pre-registered Institutional Review Board (IRB) / Research Ethics Committee (REC) department pilot study protocol, evaluation metrics, appeals governance tracking, and algorithmic fairness audit procedure for XephyGuard. It operates under the binding guarantees of the [Integrity Charter](CHARTER.md) and the [Privacy and Ethics Statement](ETHICS.md).

---

## 1. Study Objectives & Pre-Registered Research Questions

The pilot evaluates whether a privacy-preserving, lab-scoped academic-integrity platform can detect unauthorized AI assistance with high precision while maintaining near-zero false positives, zero lost student work, and demonstrable student trust.

| ID | Research Question | Pre-Registered Acceptance Target |
|---|---|---|
| **RQ1 (Honest Session Specificity)** | What is the false-positive rate (FPR) of high-severity flags on verified honest student sessions? | `FPR <= 2.0%` overall and `<= 2.0%` within every evaluated student cohort |
| **RQ2 (Adversarial Red-Team Recall)** | What fraction of scripted red-team AI bypass scenarios (`eval/scenarios/*.yaml`) are blocked at the network gateway or detected within 5 seconds? | `>= 90%` detected or blocked (`< 10%` undetected residual bypass rate) |
| **RQ3 (Cohort Fairness & Parity)** | Does behavioral and process scoring exhibit disparate false-positive rates across typing styles, non-native typists, or students with registered accommodations? | `max_fpr_gap <= 5.0%` across cohorts; `0.0%` FPR for accommodated (`exempt=True`) and `permitted_with_disclosure` sessions |
| **RQ4 (Operational Reliability)** | Does OS-level network isolation or process termination ever result in stuck network state or lost student work? | `0` stuck isolations (`isolation_lease_s` auto-recovery verified) and `0` lost student work incidents |
| **RQ5 (Student Comprehension & Trust)** | Do students understand what XephyGuard collects, what it never collects, and how to contest a flag via the Student Transparency Portal? | `>= 80%` agreement on post-session anonymous comprehension survey |

---

## 2. IRB / Ethics Governance & Informed Consent

1. **DPIA & IRB Review Pack**: Before running any pilot session, the department administrator generates the institution-specific Ethics & DPIA Pack via `php artisan xephyguard:ethics-pack` (or `GET /ethics-pack` in the CCC admin console) and files it with the university IRB/REC.
2. **Bilingual Notice (`en` / `bn`)**: Every lab workstation displays the active mode (`Always-on`, `Practice`, or `Test`), the active AI policy (`forbidden`, `permitted_with_disclosure`, or `open`), and a link to `/notice` in both English and Bangla.
3. **Research Export Anonymization**: Research datasets are exported only with explicit IRB approval (`--confirm-irb` and two-person approval when enabled), replacing student identifiers with salted HMAC-SHA256 pseudonyms (`analysis/xg_analysis/export.py`).
4. **Retention & Legal Hold**: Raw telemetry (`events`, `key_stats`, `paste_events`, `net_events`) is automatically purged after the configured retention window (`140 days` default) via `php artisan xephyguard:purge`, producing a cryptographic SHA-256 purge proof in the audit log while preserving any session or student under `legal_hold` (such as an open appeal).

---

## 3. Experimental Design: Honest vs. Red-Team Sessions

### 3.1 Labeled Honest Sessions
- Conducted during practice labs and proctored assessments where participating students consent to labeled evaluation or complete structured programming tasks under proctor observation without AI tools.
- Cohorts are stratified across:
  - `native_typist`: touch-typists fluent in the instructional language.
  - `non_native_typist`: hunt-and-peck or non-native language typists who may exhibit irregular inter-key intervals (`iki_mean_ms`, `iki_std_ms`) or higher backspace ratios.
  - `accommodated`: students using assistive technologies, screen readers, dictation tools, or extended-time accommodations (`exempt = true`).
  - `disclosure_cohort`: students participating in `ai_policy = permitted_with_disclosure` sessions where AI tools are allowed with transparent logging.

### 3.2 Scripted & Human Red-Team Sessions
- Executed via `eval/harness/redteam.py` against `eval/scenarios/linux.yaml` and `eval/scenarios/windows.yaml`, covering:
  - Cloud AI web access (`web.chatgpt`, `web.copilot`, `search.ai_overview`).
  - Network bypass attempts (`bypass.doh`, `bypass.quic`, `bypass.proxy_env`, `bypass.vpn_tun`, `bypass.tor`, `bypass.hosts_file`).
  - Local LLM inference (`local.ollama`, `local.llamacpp`, `local.container`).
  - IDE extensions and CLI agents (`ide.copilot_ext`, `cli.claude_code`).
  - Behavioral anomalies (`behavior.large_paste`, `behavior.phone_hotspot`) and agent tamper attempts (`tamper.stop_agent`, `tamper.clock`).

---

## 4. Appeals Workflow & Overturn Rate Tracking

Every high-severity alert or suspicion score can be inspected and contested by the student through the **Student Transparency Portal** (`/student-portal?token=<portal_token>`):

1. **Evidence Inspection**: The student views the exact timestamped signals, their `confidence` level (`high`, `medium`, `low`), `what_it_means`, `what_it_does_not_mean`, and their **Process & AI Disclosure Work Journal**.
2. **Appeal Submission**: Submitting an appeal (`POST /student-portal/appeals`) snapshots the evidence, places an automatic `legal_hold` on the session and student record so retention purges cannot delete contested evidence, and notifies the instructor.
3. **Audited Decision & Overturn Rate**: Instructors review the appeal at `/appeals` and record a reasoned decision (`upheld`, `overturned`, or `noted`).
4. **Automatic Rule Demotion Gate**:
   - The CCC tracks the institutional **Overturn Rate**:
     $$\text{Overturn Rate} = \frac{\text{Appeals Overturned}}{\text{Appeals Decided}}$$
   - Any detection rule whose empirical precision falls below `0.70` (`min_rule_precision = 0.70`) on labeled or appealed sessions is flagged in `low_precision_rules` and must be demoted from `high`/`critical` severity and stripped of `kill` actions before the next exam cycle.

---

## 5. Reproducible Fairness Audit Procedure

Run the automated fairness audit and governance summary using `analysis/xg_analysis/fairness.py` and `eval/export/metrics.py`:

```bash
# 1. Run the fairness audit unit and cohort parity suite
python3 -m unittest discover -s analysis/tests -p "test_fairness.py" -v

# 2. Run the evaluation harness governance checks
python3 -c "import sys; sys.path.insert(0, 'eval/tests'); import test_harness; test_harness.test_pilot_governance_summary_and_fairness()"
```

### Fairness Gate Criteria (`passes_fairness_gate`)
A release or scoring model passes the XephyGuard fairness gate if and only if:
1. Every cohort with honest sessions has `false_positive_rate <= 0.02` (`2.0%`).
2. The maximum false-positive rate gap across cohorts satisfies `max_fpr_gap <= 0.05` (`5.0%`).
3. Accommodated students (`exempt = True`) and `permitted_with_disclosure` / `open` sessions receive `effective_score == 0.0` (`0.0%` punitive flag rate).
4. All high-severity rules maintain precision `>= 0.70` (`low_precision_rules == []`).
