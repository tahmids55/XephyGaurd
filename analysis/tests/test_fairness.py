from __future__ import annotations

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from xg_analysis.fairness import audit_fairness, effective_score  # noqa: E402


class FairnessAuditTests(unittest.TestCase):
    def test_exempt_and_disclosure_sessions_always_score_zero(self) -> None:
        self.assertEqual(
            effective_score({"student_id": "s1", "score": 92.5, "exempt": True, "ai_policy": "forbidden"}),
            0.0,
        )
        self.assertEqual(
            effective_score(
                {"student_id": "s2", "score": 88.0, "exempt": False, "ai_policy": "permitted_with_disclosure"}
            ),
            0.0,
        )
        self.assertEqual(
            effective_score({"student_id": "s3", "score": 75.0, "exempt": False, "ai_policy": "open"}),
            0.0,
        )
        self.assertEqual(
            effective_score({"student_id": "s4", "score": 64.0, "exempt": False, "ai_policy": "forbidden"}),
            64.0,
        )

    def test_fairness_parity_and_exemption_protection(self) -> None:
        records = []
        # 50 honest native typists (0 false positives) + 5 red-team sessions (5 true positives)
        for i in range(50):
            records.append(
                {
                    "student_id": f"nat-{i}",
                    "group": "native_typist",
                    "ground_truth_honest": True,
                    "score": 12.0,
                    "exempt": False,
                    "ai_policy": "forbidden",
                }
            )
        for i in range(5):
            records.append(
                {
                    "student_id": f"nat-rt-{i}",
                    "group": "native_typist",
                    "ground_truth_honest": False,
                    "score": 85.0,
                    "exempt": False,
                    "ai_policy": "forbidden",
                    "rule_hits": ["proc.ollama"],
                }
            )

        # 50 honest non-native typists (0 false positives) + 5 red-team sessions (5 true positives)
        for i in range(50):
            records.append(
                {
                    "student_id": f"nn-{i}",
                    "group": "non_native_typist",
                    "ground_truth_honest": True,
                    "score": 18.0,
                    "exempt": False,
                    "ai_policy": "forbidden",
                }
            )
        for i in range(5):
            records.append(
                {
                    "student_id": f"nn-rt-{i}",
                    "group": "non_native_typist",
                    "ground_truth_honest": False,
                    "score": 82.0,
                    "exempt": False,
                    "ai_policy": "forbidden",
                    "rule_hits": ["proc.ollama"],
                }
            )

        # 10 accommodated students with high raw behavioral scores but exempt=True -> 0% FPR
        for i in range(10):
            records.append(
                {
                    "student_id": f"acc-{i}",
                    "group": "accommodated",
                    "ground_truth_honest": True,
                    "score": 95.0,
                    "exempt": True,
                    "ai_policy": "forbidden",
                    "rule_hits": ["behavior.burst_paste"],
                }
            )

        # 10 sessions in permitted_with_disclosure mode with AI activity -> 0% FPR
        for i in range(10):
            records.append(
                {
                    "student_id": f"disc-{i}",
                    "group": "disclosure_cohort",
                    "ground_truth_honest": True,
                    "score": 90.0,
                    "exempt": False,
                    "ai_policy": "permitted_with_disclosure",
                    "rule_hits": ["proc.ollama"],
                }
            )

        report = audit_fairness(records, score_threshold=50.0, min_rule_precision=0.70)
        self.assertTrue(report["passes_fairness_gate"])
        self.assertEqual(report["max_fpr_gap"], 0.0)
        self.assertEqual(report["groups"]["accommodated"]["false_positive_rate"], 0.0)
        self.assertEqual(report["groups"]["disclosure_cohort"]["false_positive_rate"], 0.0)
        self.assertEqual(report["groups"]["native_typist"]["true_positive_rate"], 1.0)
        self.assertEqual(report["groups"]["non_native_typist"]["true_positive_rate"], 1.0)
        self.assertEqual(report["low_precision_rules"], [])

    def test_detects_fpr_gap_violation_and_low_precision_rules(self) -> None:
        records = [
            # Native typists: 10 honest, 0 flagged -> FPR 0.0
            *[
                {
                    "student_id": f"n-{i}",
                    "group": "native_typist",
                    "ground_truth_honest": True,
                    "score": 10.0,
                    "exempt": False,
                    "ai_policy": "forbidden",
                }
                for i in range(10)
            ],
            # Non-native typists: 10 honest, 3 flagged -> FPR 0.30 (> 0.02 and gap 0.30 > 0.05)
            *[
                {
                    "student_id": f"nn-{i}",
                    "group": "non_native_typist",
                    "ground_truth_honest": True,
                    "score": 75.0 if i < 3 else 15.0,
                    "exempt": False,
                    "ai_policy": "forbidden",
                    "rule_hits": ["behavior.noisy_keystats"] if i < 3 else [],
                }
                for i in range(10)
            ],
            # 1 true positive for behavior.noisy_keystats and proc.ollama
            {
                "student_id": "rt-1",
                "group": "native_typist",
                "ground_truth_honest": False,
                "score": 90.0,
                "exempt": False,
                "ai_policy": "forbidden",
                "rule_hits": ["behavior.noisy_keystats", "proc.ollama"],
            },
        ]

        report = audit_fairness(records, score_threshold=50.0, min_rule_precision=0.70)
        self.assertFalse(report["passes_fairness_gate"])
        self.assertAlmostEqual(report["groups"]["non_native_typist"]["false_positive_rate"], 0.30)
        self.assertAlmostEqual(report["max_fpr_gap"], 0.30)
        self.assertIn("behavior.noisy_keystats", report["low_precision_rules"])
        self.assertNotIn("proc.ollama", report["low_precision_rules"])
        self.assertAlmostEqual(report["rule_precision"]["behavior.noisy_keystats"]["precision"], 0.25)
        self.assertAlmostEqual(report["rule_precision"]["proc.ollama"]["precision"], 1.0)


if __name__ == "__main__":
    unittest.main()
