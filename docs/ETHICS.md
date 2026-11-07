# Privacy and ethics statement

*For the university's ethics review. Adapt names, contacts and retention periods to your institution.*
*Guided by the [XephyGuard Integrity Charter](CHARTER.md).*

## Purpose
XephyGuard keeps lab **tests** fair by blocking AI tools and making their use visible, while leaving
students free to browse and learn during normal lab time. It produces research evidence on how well
such controls work.

## What is collected

| Data | Always-on (no session) | Practice | Test |
|---|---|---|---|
| AI-related programs, extensions, local model files, VM/container use | ✓ | ✓ | ✓ |
| Network adapter changes (hotspot, VPN), USB devices | ✓ | ✓ | ✓ |
| Name of the focused application | – | ✓ | ✓ |
| Window titles | – | off / app name (default) | configurable |
| Typing rhythm per 5 s (counts and intervals — **never which keys**) | – | ✓ | ✓ |
| Copy/paste size, line count, "looks like code" flag — **never the text** | – | ✓ | ✓ |
| Work-folder save timestamps and size changes — **never file contents** | – | – | ✓ |
| Blocked website attempts (host name) | – | if blocking | ✓ |
| Visited hosts/paths of allowed sites | – | off by default | off by default |

**Never collected:** typed characters, clipboard text, screenshots or screen content, passwords,
personal messages, file contents, microphone or camera. Keystrokes and clipboard text are reduced to
numbers on the PC (`agent/core/src/keystats.cpp`, `paste.cpp`) and the originals are discarded
immediately.

## Assessment modes and AI-permitted (opt-in per instructor) sessions
In addition to the standard operational modes above (`Always-on`, `Practice`, and `Test`), XephyGuard
supports pedagogical diversity by allowing each instructor to configure how AI tools are treated per
assessment:

* **AI-forbidden (`Test` default):** External AI web domains, APIs, and IDE AI backends are blocked at
  the gateway; local AI processes and ports are detected (and optionally terminated if configured in
  the profile).
* **AI-assisted with disclosure (opt-in per instructor):** When an instructor explicitly enables this
  mode for a session, students may use an institution-approved AI gateway (such as a campus-hosted
  model or an approved API routed through the CCC proxy). Usage metadata is logged transparently and
  made visible to both the student and the instructor as a shared disclosure record, supporting
  honest, accountable AI-assisted learning rather than blanket prohibition.
* **AI-open (`Practice`):** AI services remain accessible so students can experiment and learn;
  activity is logged according to the `Practice` column of the collection table without intrusive
  popups or process termination.

## Fail-safe enforcement and network isolation lease
XephyGuard is engineered so that a software or network failure never leaves a student's computer
unusable or causes lost work:

* **Bounded isolation lease:** When an instructor or assistant isolates a lab PC during a test, the
  isolation is governed by a time-bounded **isolation lease** (default `600` seconds, renewed by the
  CCC on each heartbeat while the isolation remains active).
* **Exact route save and automatic restore:** Before applying isolation, the agent saves the PC's
  exact default route(s) and firewall state to `<state_dir>/isolation_saved.json`. If the lease
  expires without renewal (for example, if the CCC becomes unreachable), or if the agent service
  restarts after a crash or reboot, the agent automatically restores the saved network configuration
  and notifies the student.
* **No destructive false positives:** Web browsers are never terminated outside active `Test`
  sessions, and weak corroborating signals (such as window-title keyword matches) never trigger
  automatic process termination or network isolation.

## Transparency
* An always-visible indicator on every PC ("Monitored by XephyGuard · mode: Test").
* A login banner on Windows and Linux.
* A public page on the CCC (`http://<ccc>/notice`) explaining the table above.
* Students may ask to see the data recorded about them.

## Decisions
XephyGuard only raises **flags for human review**. Every score lists the signals that produced it and
what the system could not observe. No sanction is applied automatically. Instructors are asked to
discuss flags with the student before acting. Students with accommodations can be marked exempt; their
behavioral signals are then ignored and the exemption is logged.

## Retention and access
* Raw events: deleted after the configured period (default 140 days ≈ one semester).
* Reports and scores: kept for the configured period (default 5 years) for academic-integrity appeals.
* Access by role (admin / instructor / assistant); every action is recorded in an append-only audit log.

## Research use
Research data is exported **only for students with a current research consent**, under a random
pseudonym, without names, logins or student numbers. Withdrawal of consent excludes the student from
all later exports. Participation in research has no effect on grades; the monitoring itself applies
to all students as a course policy, independent of research consent.

## Global compliance
XephyGuard is designed for global university deployments. Because all data is stored on-campus with
strict on-device minimization and human-in-the-loop review, institutions can map XephyGuard directly
to their national and partner data-protection frameworks:

| Jurisdiction / Framework | Key Legal Requirements | How XephyGuard Satisfies Them |
|---|---|---|
| **EU GDPR** *(Regulation (EU) 2016/679)* | Lawful basis (Art. 6), data minimization (Art. 5(1)(c)), transparency (Arts. 13–14), storage limitation (Art. 5(1)(e)), ban on solely automated decisions with significant effects (Art. 22), DPIA (Art. 35) | **Lawful basis:** Course/exam monitoring operates under Art. 6(1)(e) (public task in higher education) or Art. 6(1)(f) (legitimate interest in exam integrity); research exports are strictly separated under Art. 6(1)(a) opt-in consent. **Minimization:** Keystrokes and clipboard text are reduced to numeric summaries in RAM on the PC; `Always-on` collects inventory only. **Art. 22:** No automatic sanctions; every flag requires human review and supports contestability/appeal. **Storage limitation:** Automated scheduled purge of raw events (default 140 days). |
| **UK GDPR & Data Protection Act 2018** | UK ICO fairness, transparency, Public Task / Legitimate Interests assessment, DPIA, Data Subject Access Requests (SAR), human review | Mirrors EU GDPR controls. Provides clear pre-session notice via OS login banner, tray indicator, and `/notice`. Role-based access and append-only audit logs support institutional SAR responses and DPIA documentation. |
| **United States — FERPA** *(20 U.S.C. § 1232g; 34 CFR Part 99)* | Protection of *Education Records*, restriction of disclosure to school officials with legitimate educational interest (34 CFR § 99.31(a)(1)), student right to inspect and seek amendment, de-identification/consent for research (34 CFR § 99.31(b)) | **On-campus custody:** All identifiable session records remain on university-controlled servers with zero third-party cloud disclosure. **Access control:** Role-based permissions limit access to instructors and assistants assigned to the course/lab. **Inspection & appeal:** Students may review recorded flags/signals and contest inaccurate records. **Research:** Exports strip all PII and use random pseudonyms only for consented students. |
| **Bangladesh Personal Data Protection Law** *(Cyber Security Act / draft Personal Data Protection Act & national ICT regulations)* | Notice and consent/lawful authority, purpose limitation, data localization / sovereignty, security safeguards, right of access and correction | **Flag — Verify current status with legal counsel:** *Institutions in Bangladesh must verify the current enactment, gazetted rules, and commencement status of Bangladesh's personal data protection legislation with qualified local legal counsel prior to production deployment.* XephyGuard satisfies core national requirements by keeping 100% of data on-campus within Bangladesh (zero cross-border transfer), providing transparent notices (supporting English and Bangla), enforcing mTLS + RBAC + audit logs, and separating course-policy monitoring from optional consented research. |
| **India — Digital Personal Data Protection Act, 2023 (DPDP Act 2023)** | Itemized notice (Sec. 5), lawful purpose & consent (Secs. 4, 6, 7), Data Fiduciary security obligations (Sec. 8), erasure upon purpose completion (Sec. 8(7)), grievance redressal (Sec. 13) | **Notice:** `/notice` and login banners itemize every collected signal and its purpose. **Security:** Mutual TLS, certificate pinning, restricted database access, and tamper-evident audit logs fulfill Sec. 8 safeguards. **Retention & Erasure:** Scheduled retention purge removes raw personal data once the academic term expires. **Redressal:** Built-in human review and appeal workflow supports student grievance resolution. |
| **Other Jurisdictions** *(e.g., Canada FIPPA/PIPEDA, Australia Privacy Act / APPs, Singapore PDPA, Brazil LGPD)* | Notice before collection, proportionality, local custody, security safeguards, retention limits, access/appeal rights | Before starting a pilot in any jurisdiction, institutional counsel and the ethics board (IRB/REC) should: (1) confirm the lawful basis distinguishing exam administration from research; (2) adapt `/notice`, the login banner, and retention periods to local statutory minimums; (3) confirm on-campus server residency; and (4) designate the institutional Data Protection Officer (DPO) or privacy contact on the `/notice` page and consent form. |

---

## Consent form template (research use)

**Study:** Measuring the effectiveness of AI-use controls in programming labs
**Researchers:** ______________________ **Contact:** ______________________
**Ethics approval no.:** ______________

During lab sessions the lab computers record the information described in the table above as part of
the course policy. This form asks only whether that information may **also** be used for research.

* If you agree, your data will be analysed under a pseudonym (e.g. `P-3F9A21C0`). Results are
  published only in aggregate. No names, logins or student numbers leave the university.
* You may withdraw at any time by telling the course staff. Data already published in aggregate
  cannot be removed; future exports will exclude you.
* Saying no has **no effect** on your grades or on how you are treated in the course.

☐ I agree that my lab-session data may be used for research as described.
☐ I do not agree.

Name: ______________________ Student no.: ______________ Date: __________ Signature: __________

*(Staff record the answer under Roster → Research consent; the form version is stored with it.)*
