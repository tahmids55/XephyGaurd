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
| Connections to an outside host on a tunnel port (SSH, VPN, SOCKS, Tor): program name, destination address and port — **never the traffic** | – | ✓ | ✓ |
| Name of the focused application | – | ✓ | ✓ |
| Window titles | – | off / app name (default) | configurable |
| Typing rhythm per 5 s (counts and intervals — **never which keys**) | – | ✓ | ✓ |
| Copy/paste size, line count, "looks like code" flag — **never the text** | – | ✓ | ✓ |
| Work-folder save timestamps and size changes — **never file contents** | – | – | ✓ |
| Blocked website attempts (host name) | – | if blocking | ✓ |
| Visited hosts/paths of allowed sites | – | off by default | off by default |
| Live screen view by lab staff (on demand, not stored; every view is logged in `screen_view_audits`) | on request | on request | on request |
| Streaming cost while viewed: frames/s, kbit/s, CPU % and throttle level (numbers only, in the heartbeat) | on request | on request | on request |
| Screenshot saved as evidence by an instructor, only from their own open audited live view (with a note; saving and every viewing audited; deleted with raw data) | on request | on request | on request |

**Never collected:** typed characters, clipboard text, continuous screen recordings, passwords,
personal messages, file contents, microphone or camera. Keystrokes and clipboard text are reduced to
numbers on the PC (`agent/core/src/keystats.cpp`, `paste.cpp`) and the originals are discarded
immediately.
The CCC enforces the same rules again on arrival: it stores only the fields listed per event type in
docs/PROTOCOL.md and drops window titles the PC's title setting does not allow, so a modified agent
cannot widen what is kept.

Monitor layout (screen Step 35): the number of monitors and each one's position and size in pixels (a few numbers), sent to the viewer during a staff-requested live view so it can offer a monitor picker; no names, no content, not stored.

Live view pointer position (screen Step 34): two numbers (x, y), up to 30 per second, sent only during a staff-requested live view and never while the screen is masked; not stored, no cursor image, not logged.

## Screen privacy mask
A live screen view never shows the content of windows on the CCC's privacy list (password managers, online banking,
private browser windows and similar; the list is set by the lab admins, not built into the agent). While the focused
window matches the list, the PC sends a grey "masked" placeholder instead of pixels, and the viewer sees that the
picture is hidden. The mask also applies when the PC cannot tell which window is focused or the list cannot be read:
the picture is hidden, never shown. To decide this, the session helper keeps the name and title of the focused
window in memory on the PC; they are not sent anywhere for this purpose and are discarded when the helper stops.
Window names and titles are sent to the CCC only under the existing "Name of the focused application" and
"Window titles" settings above.

## Transparency
* An always-visible indicator on every PC ("Monitored by XephyGuard · mode: Test").
* A login banner on Windows and Linux.
* A public page on the CCC (`http://<ccc>/notice`) explaining the table above.
* The notice text (indicator and `/notice`) is the lab's own text plus sentences generated from the screen features
  that are really on (live view, screenshot evidence), so a custom text cannot leave them out.
* Students may ask to see the data recorded about them.

## Decisions
XephyGuard only raises **flags for human review**. Every score lists the signals that produced it and
what the system could not observe. No sanction is applied automatically. Instructors are asked to
discuss flags with the student before acting. Students with accommodations can be marked exempt; their
behavioral signals are then ignored and the exemption is logged.

## Retention and access
* Raw events, commands (including message text sent to students) and evidence images: deleted after the configured period (default 140 days ≈ one semester).
* Reports, scores, alerts, screen-view audits and student–PC assignments: kept for the configured period (default 5 years) for academic-integrity appeals.
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
