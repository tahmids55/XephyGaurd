# Accessibility (WCAG 2.2 AA) & Student Accommodations Guide

XephyGuard is engineered so that academic-integrity monitoring never penalizes students with disabilities, assistive-technology users, or non-native typists. This document defines our **WCAG 2.2 Level AA** accessibility posture, assistive-technology accommodation workflow, and behavioral scoring safeguards.

---

## 1. WCAG 2.2 Level AA Compliance Posture

All student-facing and instructor-facing surfaces—including the `/notice` transparency page, the **Student Transparency Portal** (`/student-portal`), the **Live Seat Map**, and desktop helper notifications—follow WCAG 2.2 Level AA requirements:

| WCAG 2.2 Criterion | XephyGuard Implementation |
|---|---|
| **1.1.1 Non-text Content** | Status icons, seat-map badges, and tray indicators always include explicit text labels (`Always-on`, `Practice`, `Test`, `Network Isolated`) so screen readers never rely on icon shape alone. |
| **1.3.1 Info and Relationships** | Semantic HTMLlandmarks (`<main>`, `<nav>`, `<section>`, `<table>` with `<th>` scopes) and associated `<label>` elements across all forms (enrollment, session wizard, student portal, appeals). |
| **1.4.1 Use of Color** | Severity and mode indicators pair high-contrast color tokens with text badges (`[HIGH]`, `[MEDIUM]`, `[DISCLOSURE]`, `[INSTALLED ONLY]`) so color-blind users can distinguish every state. |
| **1.4.3 Contrast (Minimum)** | Dark and light UI palettes maintain a minimum contrast ratio of `>= 4.5:1` for normal body text and `>= 3.0:1` for large text and UI components. |
| **2.1.1 Keyboard Accessible** | Every action in `/notice`, `/student-portal`, `/sessions`, and `/appeals` is operable via standard keyboard navigation (`Tab`, `Shift+Tab`, `Enter`, `Space`) without mouse traps. |
| **2.4.7 Focus Visible** | Interactive controls preserve visible focus outlines for keyboard and switch-device navigation. |
| **3.1.1 / 3.1.2 Language of Page & Parts** | Full bilingual support (`en` English and `bn` Bangla) via `/lang/{locale}` with explicit `lang` attributes for accurate screen-reader pronunciation. |
| **3.3.1 / 3.3.2 Error Identification & Labels** | Form validation errors provide clear text explanations next to the relevant field, and appeal forms clearly explain required fields. |

---

## 2. Assistive Technology & Disability Accommodation Workflow

Students who use assistive software—such as screen readers (`NVDA`, `JAWS`, `Orca`), voice dictation (`Dragon`, OS speech-to-text), on-screen keyboards, switch devices, or magnification utilities—can exhibit process trees, clipboard bursts, or keystroke-timing distributions that differ substantially from standard keyboard input.

### 2.1 Pre-Session Accommodation Registration
1. **Roster / LTI Import or Instructor Toggle**: Before or during a lab session, the instructor or disability-services coordinator marks the student or workstation as **Accommodated / Exempt** on the session roster (`exempt = true`) or adds assistive binaries to the profile's `kill_exempt` list.
2. **Process-Kill Protection**:
   - Assistive binaries listed in `kill_exempt` (alongside OS-critical processes) are **never terminated** by the C++ agent (`Agent::handle_rule_hit`), even in `Test` mode.
3. **Behavioral Anomaly Zeroing**:
   - In both server-side scoring and the offline fairness auditor (`xg_analysis.fairness.effective_score`), any record with `exempt = True` is guaranteed `effective_score == 0.0`.
   - Dictation bursts (which can appear as rapid character insertion without physical keystrokes) never trigger a high-severity behavioral flag for an accommodated student.

### 2.2 Post-Session Retroactive Accommodation via Appeals
If a student with an unreported accommodation is flagged during a session:
1. The student opens `/student-portal?token=<portal_token>`, reviews the exact signal, and submits an appeal noting their accommodation.
2. Submitting the appeal places an automatic `legal_hold` on the session and student record.
3. The instructor overturns the flag (`status = overturned`), marks the student record as accommodated, and the overturned alert is excluded from any misconduct report.

---

## 3. Non-Native Typist & Diverse Typing Style Protections

Research shows that hunt-and-peck typists, multilingual students composing in a second language, and students who draft code in a scratch buffer before pasting into their main file can trigger naive keystroke-dynamics detectors. XephyGuard enforces four structural safeguards:

1. **No Raw Keystroke or Clipboard Content Capture**: XephyGuard never records which keys were pressed or what text was copied/pasted—only aggregate 5-second window statistics (`iki_mean_ms`, `iki_std_ms`, `backspace_ratio`, and clipboard line/character counts).
2. **Corroboration Requirement ([Charter Principle 2](CHARTER.md))**: Behavioral timing (`key_stats`) and paste size (`paste_events`) are always classified as `low` or `medium` confidence signals. They **cannot** trigger process termination or network isolation, and they never produce a high-severity accusation without corroborating high-confidence process or network evidence.
3. **Process-Evidence Work Journal**: When file-activity collection is active in `Test` mode or `permitted_with_disclosure` mode, the student's incremental file saves and size progression in the designated work directory (`~/lab` or `%USERPROFILE%\Documents\Lab`) are recorded in the **Process & AI Disclosure Work Journal**, giving students affirmative evidence of steady, authentic work progression.
4. **Mandatory Cohort Fairness Gate**: Before any scoring threshold is deployed, `analysis/xg_analysis/fairness.py` verifies that `non_native_typist` sessions have `false_positive_rate <= 2.0%` and that the false-positive gap between `native_typist` and `non_native_typist` cohorts is `<= 5.0%`.
