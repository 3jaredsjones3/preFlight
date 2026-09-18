"""Test the qualification runner's failure behavior without pretending to slice."""
import argparse
import gzip
import json
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import jsonschema
from unittest.mock import patch

sys.dont_write_bytecode = True
import predictive_golden as runner

COMPARATOR = Path(sys.argv.pop(1)).resolve()
ROOT = Path(__file__).resolve().parents[1]
PROGRAM = "G90\nM83\nT0\nG1 X10 E0.5 F1200\n; printing object test id:0 copy 0\n"


def snapshot(job):
    data = json.loads((ROOT / "tests/predictive/golden" / f"{job}.artifact.json").read_text(encoding="utf-8"))
    # This synthetic runner test uses the common representation;
    # production generator behavior is tested only by real slicing.
    if job == "mechanical":
        for entity in data["source_entities"]:
            entity["context"]["generator"] = "athena"
    if job in ("mechanical", "thin_wall"):
        for entity in data["source_entities"]:
            entity["context"]["source"] = "perimeters"
    return data


class RunnerTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            baseline, candidate = directory / "baseline.exe", directory / "candidate.exe"
            baseline.write_bytes(b"synthetic baseline")
            candidate.write_bytes(b"synthetic candidate")
            fixture_jobs = directory / "jobs"
            shutil.copytree(ROOT / "tests/predictive/jobs", fixture_jobs)
            manifest_path = fixture_jobs / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            # Runner unit tests use synthetic snapshots with their own coverage;
            # the production manifest has stronger native slicing expectations.
            expectations = {"mechanical": {"generator": ["athena"]}, "thin_wall": {"generator": ["arachne"]},
                            "bridge_support": {"role_bits": [8]}, "multi_object": {"object": [0, 1]},
                            "serpentine": {"role_bits": [4161]}, "interlocking": {"role_bits": [2049]},
                            "multi_material": {"configured_tool": [0, 1]}}
            for job in manifest["jobs"]:
                job["expect"] = expectations[job["name"]]
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            args = argparse.Namespace(manifest=manifest_path,
                                      baseline=baseline, candidate=candidate, comparator=COMPARATOR,
                                      output=directory / "results")
            args.keep_going = fault == "continue_gcode"
            if args.keep_going:
                fault = "gcode"
            if fault in ("accepted", "changed_accepted", "changed_accepted_report", "missing_accepted_report"):
                args.accepted = directory / "accepted"
                args.accepted.mkdir()
                for job in json.loads(args.manifest.read_text(encoding="utf-8"))["jobs"]:
                    program = PROGRAM.replace("E0.5", "E0.4") if fault == "changed_accepted" else PROGRAM
                    (args.accepted / f"{job['name']}.gcode.gz").write_bytes(gzip.compress(program.encode("utf-8"), mtime=0))
                    report = json.dumps(snapshot(job['name'])).encode("utf-8")
                    if fault == "changed_accepted_report":
                        report += b" "
                    if fault != "missing_accepted_report":
                        (args.accepted / f"{job['name']}.artifact.json.gz").write_bytes(gzip.compress(report, mtime=0))
            original_run = subprocess.run
            calls = []

            def fake_slicer(command, **kwargs):
                if Path(command[0]) == COMPARATOR:
                    return original_run(command, **kwargs)
                calls.append(command)
                gcode = Path(command[command.index("--output") + 1])
                program = PROGRAM
                if fault == "gcode" and gcode.stem == "analysis":
                    program = program.replace("E0.5", "E0.6")
                gcode.write_text(program, encoding="utf-8")
                if fault == "ordinary_report" and command[1] == "--export-gcode":
                    Path(str(gcode) + ".artifact.json").write_text("{}", encoding="utf-8")
                if command[1] == "--predictive-analysis" and fault != "missing_report":
                    job = gcode.parent.name
                    data = snapshot(job)
                    if fault == "nondeterministic" and gcode.stem == "repeat":
                        data["source_entities"][0]["context"]["source"] = "changed"
                    if fault == "wrong_feature":
                        for entity in data["source_entities"]:
                            entity["context"]["generator"] = "unexpected"
                    if fault == "schema":
                        data["unexpected_schema_field"] = True
                    Path(str(gcode) + ".artifact.json").write_text(json.dumps(data), encoding="utf-8")
                return subprocess.CompletedProcess(command, 0)

            with patch.object(runner.subprocess, "run", side_effect=fake_slicer):
                try:
                    runner.run_job(args)
                except ValueError:
                    if args.keep_going:
                        evidence = json.loads((args.output / "evidence.json").read_text(encoding="utf-8"))
                        self.assertEqual(len(calls), 28)
                        self.assertEqual(len(evidence["jobs"]), 7)
                        self.assertTrue(all(row["status"] == "failed" for row in evidence["jobs"]))
                    raise
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

    def test_rejects_schema_violation(self):
        with self.assertRaises(jsonschema.ValidationError):
            self.exercise("schema")

    def test_ordinary_mode_has_no_report(self):
        with self.assertRaisesRegex(ValueError, "ordinary mode"):
            self.exercise("ordinary_report")

    def test_accepted_baselines(self):
        self.exercise("accepted")

    def test_rejects_changed_accepted_baseline(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.exercise("changed_accepted")

    def test_rejects_changed_accepted_report(self):
        with self.assertRaisesRegex(ValueError, "differs from accepted baseline"):
            self.exercise("changed_accepted_report")

    def test_rejects_missing_accepted_report(self):
        with self.assertRaises(FileNotFoundError):
            self.exercise("missing_accepted_report")

    def test_keep_going_records_all_failures_and_still_fails(self):
        with self.assertRaisesRegex(ValueError, "failed production jobs"):
            self.exercise("continue_gcode")

    def test_generator_metadata_alone_is_not_coverage(self):
        data = {"source_entities": [{"bead_path": 1, "context": {"generator": "arachne", "source": "fills"}}]}
        with self.assertRaisesRegex(ValueError, "missing expected generator"):
            runner.check_expectations(data, {"object_labels": []}, {"generator": ["arachne"]})

    def test_requires_actual_tool_changes(self):
        with self.assertRaisesRegex(ValueError, "missing expected emitted_tools"):
            runner.check_expectations({"source_entities": []}, {"emitted_tools": [0], "object_labels": []}, {"emitted_tools": [0, 1]})

    def test_empty_brim_container_is_not_coverage(self):
        data = {"source_entities": [{"bead_path": None, "context": {"source": "brim_template"}}]}
        with self.assertRaisesRegex(ValueError, "missing expected source"):
            runner.check_expectations(data, {"object_labels": []}, {"source": ["brim_template"]})


if __name__ == "__main__":
    unittest.main()
