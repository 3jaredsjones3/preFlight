"""Test the qualification runner's failure behavior without pretending to slice."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
import predictive_golden as runner

COMPARATOR = Path(sys.argv.pop(1)).resolve()
ROOT = Path(__file__).resolve().parents[1]


class RunnerTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            baseline, candidate = directory / "baseline.exe", directory / "candidate.exe"
            baseline.write_bytes(b"synthetic baseline")
            candidate.write_bytes(b"synthetic candidate")
            args = argparse.Namespace(manifest=ROOT / "tests/predictive/jobs/manifest.json",
                                      baseline=baseline, candidate=candidate, comparator=COMPARATOR,
                                      output=directory / "results")
            original_run = subprocess.run
            calls = []

            def fake_slicer(command, **kwargs):
                if Path(command[0]) == COMPARATOR:
                    return original_run(command, **kwargs)
                calls.append(command)
                gcode = Path(command[command.index("--output") + 1])
                program = "G90\nM83\nT0\nG1 X10 E0.5 F1200\n; printing object test id:0 copy 0\n"
                if fault == "gcode" and gcode.stem == "analysis":
                    program = program.replace("E0.5", "E0.6")
                gcode.write_text(program, encoding="utf-8")
                if command[1] == "--predictive-analysis" and fault != "missing_report":
                    job = gcode.parent.name
                    data = json.loads((ROOT / "tests/predictive/golden" / f"{job}.artifact.json").read_text(encoding="utf-8"))
                    # This synthetic runner test uses the common representation;
                    # production generator behavior is tested only by real slicing.
                    if job == "mechanical":
                        for entity in data["source_entities"]:
                            entity["context"]["generator"] = "athena"
                    if fault == "nondeterministic" and gcode.stem == "repeat":
                        data["source_entities"][0]["context"]["source"] = "changed"
                    if fault == "wrong_feature":
                        for entity in data["source_entities"]:
                            entity["context"]["generator"] = "unexpected"
                    Path(str(gcode) + ".artifact.json").write_text(json.dumps(data), encoding="utf-8")
                return subprocess.CompletedProcess(command, 0)

            with patch.object(runner.subprocess, "run", side_effect=fake_slicer):
                runner.run_job(args)
            self.assertEqual(len(calls), 28)
            self.assertEqual([call[1] for call in calls[:4]], ["--export-gcode", "--export-gcode", "--predictive-analysis", "--predictive-analysis"])
            evidence = json.loads((args.output / "evidence.json").read_text(encoding="utf-8"))
            self.assertEqual(len(evidence["jobs"]), 7)
            self.assertEqual(len(evidence["executables_sha256"]), 3)

    def test_qualification_workflow(self):
        self.exercise()

    def test_rejects_changed_extrusion(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.exercise("gcode")

    def test_rejects_nondeterministic_report(self):
        with self.assertRaisesRegex(ValueError, "not deterministic"):
            self.exercise("nondeterministic")

    def test_rejects_missing_report(self):
        with self.assertRaises(FileNotFoundError):
            self.exercise("missing_report")

    def test_rejects_wrong_feature(self):
        with self.assertRaisesRegex(ValueError, "missing expected"):
            self.exercise("wrong_feature")


if __name__ == "__main__":
    unittest.main()
