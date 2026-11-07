<?php

namespace App\Services;

use App\Models\Lab;
use App\Models\Profile;
use App\Models\Setting;

/**
 * Generates a complete, institution-specific Data Protection Impact Assessment (DPIA)
 * and IRB Ethics-Review Pack (P2-C2) from the live XephyGuard CCC configuration.
 */
class EthicsPackGenerator
{
    public function buildData(): array
    {
        $lab = Lab::query()->first();
        $labName = Setting::get('lab_name', $lab?->name ?? config('xephyguard.lab_name', 'Computer Lab'));
        $retentionRawDays = (int) Setting::get('retention_days_raw', config('xephyguard.retention_raw_days', 140));
        $retentionReportDays = (int) Setting::get('retention_days_reports', config('xephyguard.retention_days_reports', 1825));
        $isolationLeaseSeconds = (int) config('xephyguard.isolation_lease_seconds', 600);
        $isolationLeaseMinutes = max(1, (int) round($isolationLeaseSeconds / 60));
        $inspectHosts = Setting::get('inspect_hosts', NetworkPolicyWriter::DEFAULT_INSPECT_HOSTS);
        $twoPersonRule = (bool) Setting::get('two_person_rule', config('xephyguard.two_person_rule', false));
        $profiles = Profile::query()->orderBy('mode')->orderBy('name')->get();

        $tieredCollection = PolicyBuilder::TIER_COLLECT;
        $complianceMatrix = [
            [
                'framework' => 'GDPR (EU/UK) Art. 5, 6, 22, 35',
                'requirement' => 'Data minimization, transparency, DPIA, prohibition on solely automated punitive decisions',
                'controls' => "Tiered collection ceiling; no keystroke/clipboard/screen capture; bilingual /notice & Student Transparency Portal; human-in-the-loop review with Appeals workflow; {$retentionRawDays}-day raw purge with cryptographic proof.",
            ],
            [
                'framework' => 'FERPA (US 20 U.S.C. § 1232g)',
                'requirement' => 'Protection of student education records, student right to inspect and contest records',
                'controls' => 'On-premises CCC storage (zero cloud third-party telemetry); Student Self-Service Transparency Portal (/student-portal) with JSON export and formal appeal workflow.',
            ],
            [
                'framework' => 'Bangladesh Cyber Security Act & ICT Governance',
                'requirement' => 'Lawful institutional network administration, proportionate monitoring, bilingual notice',
                'controls' => 'Bilingual English & Bangla (/notice?lang=bn) transparency banner; strict separation of Lab Root CA and Name-Constrained Search-Only Interception Sub-CA; zero private message or credential interception.',
            ],
            [
                'framework' => 'India DPDP Act 2023',
                'requirement' => 'Itemized notice, purpose limitation, erasure upon retention expiry, grievance redressal',
                'controls' => "Live itemized collection table on /notice; automated {$retentionRawDays}-day retention purge with legal-hold protection; Student Appeals Portal.",
            ],
            [
                'framework' => 'EU AI Act (High-Risk Education Annex III)',
                'requirement' => 'Human oversight, explainability, accuracy/fairness auditing, logs conservation',
                'controls' => 'Explicit evidence confidence levels (strong/medium/weak) with What It Means / What It Does Not Mean; accommodation exemptions; subgroup fairness parity audit (FPR gap & 80% disparate impact check).',
            ],
        ];

        return [
            'generated_at' => now()->toIso8601String(),
            'lab_name' => $labName,
            'subnet' => $lab?->subnet ?? Setting::get('lab_subnet', config('xephyguard.lab_subnet')),
            'ccc_ip' => $lab?->ccc_ip ?? Setting::get('ccc_ip', config('xephyguard.ccc_ip')),
            'retention_raw_days' => $retentionRawDays,
            'retention_report_days' => $retentionReportDays,
            'isolation_lease_seconds' => $isolationLeaseSeconds,
            'isolation_lease_minutes' => $isolationLeaseMinutes,
            'two_person_rule' => $twoPersonRule,
            'inspect_hosts' => $inspectHosts,
            'profiles' => $profiles,
            'tiered_collection' => $tieredCollection,
            'compliance_matrix' => $complianceMatrix,
        ];
    }

    public function toMarkdown(?array $data = null): string
    {
        $d = $data ?? $this->buildData();
        $md = [];
        $md[] = "# XephyGuard Data Protection Impact Assessment (DPIA) & IRB Ethics-Review Pack";
        $md[] = "";
        $md[] = "- **Institution / Lab Name:** {$d['lab_name']}";
        $md[] = "- **Generated At:** {$d['generated_at']}";
        $md[] = "- **Lab Network:** `{$d['subnet']}` (CCC Gateway `{$d['ccc_ip']}`)";
        $md[] = "- **Raw Telemetry Retention (`retention_raw_days`):** {$d['retention_raw_days']} days";
        $md[] = "- **Session Report Retention:** {$d['retention_report_days']} days";
        $md[] = "- **Bounded Network Isolation Lease (`isolation_lease_seconds`):** {$d['isolation_lease_seconds']} seconds ({$d['isolation_lease_minutes']} minutes)";
        $md[] = "- **Two-Person Governance Rule:** ".($d['two_person_rule'] ? 'Enabled' : 'Configurable (Available)');
        $md[] = "";
        $md[] = "## 1. Purpose, Proportionality & Never-Collected Guarantees";
        $md[] = "";
        $md[] = "XephyGuard is an on-premises university computer-lab assessment integrity and AI-disclosure governance system. It is architected around **privacy-by-architecture**:";
        $md[] = "";
        $md[] = "- **NEVER Collected:** Keystroke characters or text, clipboard contents, screenshots or screen recordings, webcam video, microphone audio, passwords, or personal communications.";
        $md[] = "- **Name-Constrained TLS Architecture:** The Lab Root CA signs a separate `codeSigning` leaf certificate for signature/update verification and a Name-Constrained (`pathlen:0`) Search-Only Interception Sub-CA restricted via critical X.509 Name Constraints exclusively to configured search domains (".implode(', ', array_slice($d['inspect_hosts'], 0, 6))."). Private banking, email, and general HTTPS sites are cryptographically exempt from interception.";
        $md[] = "- **Bounded Network Isolation Lease:** Any PC network isolation automatically expires after **{$d['isolation_lease_seconds']} seconds ({$d['isolation_lease_minutes']} minutes)** if the CCC server becomes unreachable, preserving local IDE work and student safety.";
        $md[] = "";
        $md[] = "## 2. Tiered Data Collection Matrix (Mode Ceiling)";
        $md[] = "";
        $md[] = "| Telemetry Signal | `always_on` (No Session) | `practice` (Class) | `test` (Assessment) |";
        $md[] = "|---|---|---|---|";
        $signals = array_keys($d['tiered_collection']['always_on']);
        foreach ($signals as $sig) {
            $ao = ($d['tiered_collection']['always_on'][$sig] ?? false) ? 'Yes' : 'Off';
            $pr = ($d['tiered_collection']['practice'][$sig] ?? false) ? 'Yes' : 'Off';
            $te = ($d['tiered_collection']['test'][$sig] ?? false) ? 'Yes' : 'Off';
            $md[] = "| `{$sig}` | {$ao} | {$pr} | {$te} |";
        }
        $md[] = "";
        $md[] = "## 3. Active Institutional Profiles";
        $md[] = "";
        $md[] = "| Profile Name | Mode | AI Policy (`ai_policy`) | Blocked Categories | Window Title Capture |";
        $md[] = "|---|---|---|---|---|";
        foreach ($d['profiles'] as $p) {
            $cats = implode(', ', $p->network['block_categories'] ?? []) ?: 'None';
            $tc = $p->agent['title_capture'] ?? 'app_only';
            $aiPol = $p->ai_policy ?? 'forbidden';
            $md[] = "| {$p->name} | `{$p->mode}` | `{$aiPol}` | {$cats} | `{$tc}` |";
        }
        $md[] = "";
        $md[] = "## 4. Retention Schedule, Legal Hold & Cryptographic Purge Proof";
        $md[] = "";
        $md[] = "- **Raw Event Retention:** Raw telemetry (`events`, `key_stats`, `paste_events`, `net_events`) is automatically purged after **{$d['retention_raw_days']} days** via `php artisan xephyguard:purge`.";
        $md[] = "- **Automatic Legal Hold:** When a student submits an appeal via `/student-portal`, XephyGuard automatically sets `legal_hold = true` on both the `Student` and `LabSession` records. The retention purge command skips all records under legal hold or with an open appeal until resolved.";
        $md[] = "- **Cryptographic Purge Proof:** Every purge execution writes an append-only `AuditLog` record (`action = 'retention.purge'`) containing a SHA-256 cryptographic proof (`proof_sha256`) over the cutoff timestamp, deleted row counts, and skipped legal-hold session IDs.";
        $md[] = "";
        $md[] = "## 5. Global Legal & Ethical Compliance Matrix";
        $md[] = "";
        $md[] = "| Framework / Regulation | Core Requirement | XephyGuard Technical & Procedural Controls |";
        $md[] = "|---|---|---|";
        foreach ($d['compliance_matrix'] as $row) {
            $md[] = "| **{$row['framework']}** | {$row['requirement']} | {$row['controls']} |";
        }
        $md[] = "";
        $md[] = "## 6. Student Rights, Accommodations & Appeals Workflow";
        $md[] = "";
        $md[] = "1. **Bilingual Real-Time Notice (`/notice`):** Displays active session mode, `ai_policy` (`forbidden`, `permitted_with_disclosure`, or `open`), and live itemized collection in English and Bangla.";
        $md[] = "2. **Student Self-Service Transparency Portal (`/student-portal`):** Students can inspect every flag, score, confidence level, *What It Means* / *What It Does Not Mean* explanation, and their Process & AI Disclosure Work Journal, download their full record as JSON, and file a formal appeal.";
        $md[] = "3. **Assistive Technology & Accommodation Exemptions:** Students with registered accommodations (`Student::exempt = true`) or per-PC profile overrides have all behavioral anomalies suppressed and their score clamped to `0.0`.";
        $md[] = "";
        $md[] = "## 7. IRB Informed Consent Form (Research Export Opt-In)";
        $md[] = "";
        $md[] = "> **Informed Consent for Pseudonymized Educational Research ({$d['lab_name']})**";
        $md[] = ">";
        $md[] = "> You are invited to optionally contribute your pseudonymized session telemetry from **{$d['lab_name']}** to an educational research study evaluating computer-lab integrity and AI-assisted learning.";
        $md[] = "> - **Voluntary Participation:** Participation is completely voluntary. Declining or withdrawing consent at any time has **zero effect** on your course grades, lab access, or academic standing.";
        $md[] = "> - **What Is Shared If You Opt In:** Only de-identified, pseudonymized session feature summaries under a random research identifier (`S-XXXXXXXX`). Your name, student ID, and OS login are never included in research exports.";
        $md[] = "> - **What Is Never Collected:** Keystrokes, clipboard text, screenshots, audio/video, or passwords are never recorded by XephyGuard.";
        $md[] = "> - **Data Retention:** Raw lab logs are purged after **{$d['retention_raw_days']} days**.";
        $md[] = ">";
        $md[] = "> `[ ] I CONSENT to include my pseudonymized session metrics in research exports.`";
        $md[] = "> `[ ] I DO NOT CONSENT (default — my data is excluded from all research exports).`";
        $md[] = "";

        return implode("\n", $md);
    }
}
