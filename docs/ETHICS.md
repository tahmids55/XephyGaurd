# Privacy and ethics statement

*For the university's ethics review. Adapt names, contacts and retention periods to your institution.*

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
