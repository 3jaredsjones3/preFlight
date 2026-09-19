#!/usr/bin/env python3
"""Deterministic adversarial tests for the isolated M2 coupon package."""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "m2_coupon.py"


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def digest(value):
    return hashlib.sha256(value).hexdigest()


class CouponTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.gcode = (self.root / "program.gcode")
        program = ("; preflight_config = begin\nG21\nG90\nM83\nM109 S210\n"
                   "G0 X10 Y10 Z0.2 F120\nG1 X20 Y10 E0.09 F2400\n"
                   "G0 X20 Y20 F120\nG1 X30 Y20 E0.09 F2400\n"
                   "M104 S0\nM140 S0\n; objects_info = {\"objects\":[]}\n; preflight_config = end\n").encode()
        self.gcode.write_bytes(program)
        self.machine = {"schema_version": "js-machine-1", "machine_id": "TEST-AD5M",
                        "dialect": "marlin-cartesian-1", "build_min_mm": [0, 0, 0], "build_max_mm": [300, 300, 300],
                        "axis_velocity_mm_s": [200, 200, 200, 120], "axis_acceleration_mm_s2": [10000, 10000, 10000, 10000],
                        "max_feedrate_mm_s": 200, "max_acceleration_mm_s2": 10000, "max_bed_temperature_c": 120,
                        "max_retraction_mm": 1000, "max_extrusion_step_mm": 200, "fan_count": 1, "heaters": [280],
                        "tools": [{"heater": 0, "offset_mm": [0, 0, 0], "envelope_min_mm": [-0.2, -0.2, 0],
                                   "envelope_max_mm": [0.2, 0.2, 1], "filament_diameter_mm": 1.75,
                                   "max_flow_mm3_s": 200, "min_extrusion_temperature_c": 170}]}
        machine_hash = digest(canonical_json(self.machine))
        self.machine_path = self.root / "machine.json"
        self.machine_path.write_bytes(canonical(self.machine))
        contract = lambda geometry: {"geometry_sha256": geometry, "extrusion_volume_mm3": 0.09,
                                     "width_mm": 0.45, "height_mm": 0.2, "speed_mm_s": 40,
                                     "temperature_c": 210, "fan_pwm": 0, "tool": 0, "material": "PLA"}
        lines = self.gcode.read_bytes().splitlines(keepends=True)
        self.paths = [{"id": "1", "command_start": 5, "command_end": 6, "source_line_start": 6, "source_line_end": 7,
                       "command_sha256": digest(b"".join(lines[5:7])), "predecessors": [],
                       "tool": 0, "material": "PLA", "coupon_contract": contract(digest(b"geom-1"))},
                      {"id": "2", "command_start": 7, "command_end": 8, "source_line_start": 8, "source_line_end": 9,
                       "command_sha256": digest(b"".join(lines[7:9])), "predecessors": [],
                       "tool": 0, "material": "PLA", "coupon_contract": contract(digest(b"geom-2"))}]
        self.manifest = {"schema_version": "js-verification-1", "fingerprint_sha256": digest(self.machine_path.read_bytes()),
                         "program_sha256": digest(self.gcode.read_bytes()), "compiler_commit": "82deceb63680afe47a5292ba99245bd86d0f1564",
                         "compiler_build_id": "m2-coupon-test", "initial_state": {"position_mm": [10, 10, 0], "e_mm": 0,
                         "tool": 0, "heater_temperature_c": [20], "bed_temperature_c": 20, "fan_pwm": [0]}, "objects": [],
                         "required_properties": ["syntax", "modal_state", "numeric", "build_volume", "tool_envelope", "axis_velocity",
                         "configured_motion_limits", "volumetric_flow", "temperature_commands", "cold_extrusion", "extrusion_continuity",
                         "object_boundaries", "fingerprint_binding", "program_integrity", "schema_compatibility"], "end_marker": "; preflight_config = end"}
        self.packet = {"schema_version": "js-work-packet-1", "canonicalization": "js-canonical-json-1",
                       "program_sha256": digest(self.gcode.read_bytes()), "fingerprint_sha256": digest(canonical_json(self.machine)),
                       "manifest_sha256": digest(canonical_json(self.manifest)), "compiler": {"commit": self.manifest["compiler_commit"], "build_id": "m2-coupon-test"},
                       "schema_versions": {"machine": "js-machine-1", "manifest": "js-verification-1", "packet": "js-work-packet-1", "report": "js-verifier-report-1"},
                       "paths": self.paths, "temporary_structures": [], "datums": []}
        self.spec = {"schema_version": "js-m2-coupon-experiment-1", "experiment": {"id": "TEST", "registration_status": "preregistered"},
                     "printer": {"model": "AD5M", "serial": "TEST-SERIAL", "identity_status": "confirmed", "machine_fingerprint_sha256": machine_hash, "synthetic": False},
                     "process": {"material_family": "PLA", "material_lot": "LOT-1", "nozzle_id": "N-1", "nozzle_diameter_mm": 0.4,
                                 "layer_height_mm": 0.2, "line_width_mm": 0.45, "print_temperature_c": 210, "bed_temperature_c": 60, "fan_percent": 0, "ambient_temperature_c": 25},
                     "model": {"model_id": "model-test", "model_hash": "model-hash", "calibration_hash": "calibration-hash"},
                     "variants": {"baseline": {"identity": "source-order"}, "proposed": {"identity": "m2-proposal"}},
                     "measurement_protocol": {"required": ["bond", "thermal", "quality"]},
                     "acceptance": {"minimum_bond_proxy_improvement_percent": 10, "visible_quality_noninferiority_delta": 0, "print_time_max_increase_percent": 5},
                     "abort_exclusion": {"criteria": ["failed adhesion"]}, "evidence": {"files": ["raw csv"]},
                     "sample_plan": {"calibration_coupon_ids": ["CAL-1"], "confirmatory_coupon_ids": ["CONF-1", "CONF-2"]},
                     "randomization": {"unit": "coupon_pair", "seed": 42}}
        self.model = {"schema_version": "js-thermal-model-1", "model_id": "model-test", "model_version": "1",
                      "machine_fingerprint_hash": machine_hash, "material_family": "PLA", "provenance": "measured",
                      "calibrated": True, "synthetic": False, "calibration_dataset_hash": "calibration-hash", "cooling_time_constant_s": 10}
        self.proposal = {"schema_version": "js-thermal-schedule-1", "machine_fingerprint_hash": machine_hash,
                         "model_id": "model-test", "model_hash": "model-hash", "analysis_only": True,
                         "recommendation_eligible": True, "baseline_order": [1, 2], "proposed_order": [2, 1]}
        for name, value in (("spec.json", self.spec), ("model.json", self.model), ("proposal.json", self.proposal),
                            ("manifest.json", self.manifest), ("packet.json", self.packet)):
            (self.root / name).write_bytes(canonical(value))

    def tearDown(self):
        self.temp.cleanup()

    def run_tool(self, *arguments):
        return subprocess.run([sys.executable, str(TOOL), *map(str, arguments)], capture_output=True, text=True, check=False)

    def prepare(self, out, permit=True):
        arguments = ["prepare", "--spec", self.root / "spec.json", "--gcode", self.gcode, "--machine", self.machine_path,
                             "--manifest", self.root / "manifest.json", "--packet", self.root / "packet.json",
                             "--proposal", self.root / "proposal.json", "--model", self.root / "model.json", "--out", out,
                             ]
        if permit:
            arguments.append("--permit-synthetic-test-fixture")
        verifier = os.environ.get("M2_COUPON_VERIFIER")
        if verifier:
            arguments.extend(["--verifier", verifier])
        return self.run_tool(*arguments)

    def test_pair_equivalence_and_repeatable_randomization(self):
        first, second = self.root / "bundle1", self.root / "bundle2"
        self.assertEqual(self.prepare(first).returncode, 0)
        self.assertEqual(self.prepare(second).returncode, 0)
        self.assertEqual((first / "proposed/program.gcode").read_bytes(), (second / "proposed/program.gcode").read_bytes())
        self.assertEqual((first / "print-order.csv").read_bytes(), (second / "print-order.csv").read_bytes())
        self.assertEqual(self.run_tool("verify-pair", "--bundle", first, "--permit-synthetic-test-fixture").returncode, 0)

    def test_rejects_dependency_violation_and_mutated_contract(self):
        self.paths[1]["predecessors"] = ["1"]
        self.packet["paths"] = self.paths
        (self.root / "packet.json").write_bytes(canonical(self.packet))
        self.proposal["proposed_order"] = [2, 1]
        (self.root / "proposal.json").write_bytes(canonical(self.proposal))
        self.assertNotEqual(self.prepare(self.root / "bad-dependency").returncode, 0)
        self.proposal["proposed_order"] = [1, 2]
        self.paths[1]["predecessors"] = []
        self.packet["paths"] = self.paths
        (self.root / "packet.json").write_bytes(canonical(self.packet))
        (self.root / "proposal.json").write_bytes(canonical(self.proposal))
        self.assertNotEqual(self.prepare(self.root / "no-difference").returncode, 0)

    def test_missing_physical_metadata_and_synthetic_model_fail_closed(self):
        self.spec["printer"]["identity_status"] = "template"
        (self.root / "spec.json").write_bytes(canonical(self.spec))
        self.assertNotEqual(self.prepare(self.root / "not-printable", permit=False).returncode, 0)

    def test_targeted_pair_mutation_rejected(self):
        bundle = self.root / "bundle"
        self.assertEqual(self.prepare(bundle).returncode, 0)
        packet = json.loads((bundle / "proposed/work-packet.json").read_text())
        packet["paths"][0]["coupon_contract"]["temperature_c"] = 211
        (bundle / "proposed/work-packet.json").write_bytes(canonical(packet))
        self.assertNotEqual(self.run_tool("verify-pair", "--bundle", bundle, "--permit-synthetic-test-fixture").returncode, 0)

    def test_evaluation_separates_samples_and_preserves_exclusions(self):
        bundle = self.root / "bundle"
        self.assertEqual(self.prepare(bundle).returncode, 0)
        complete = {"schema_version": "js-thermal-calibration-1", "measurements": [
            {"coupon_id": "CONF-1", "variant": "baseline", "bond_proxy": 10, "visible_quality_score": 4,
             "print_time_s": 100, "excluded": False},
            {"coupon_id": "CONF-1", "variant": "proposed", "bond_proxy": 12, "visible_quality_score": 4,
             "print_time_s": 101, "excluded": False},
            {"coupon_id": "CONF-2", "variant": "baseline", "bond_proxy": 10, "visible_quality_score": 4,
             "print_time_s": 100, "excluded": True, "exclusion_reason": "instrument fault"},
        ]}
        measurements = self.root / "confirmation.json"
        measurements.write_bytes(canonical(complete))
        result = self.run_tool("evaluate", "--bundle", bundle, "--measurements", measurements, "--output", self.root / "evaluation.json")
        self.assertEqual(result.returncode, 0)
        evaluation = json.loads((self.root / "evaluation.json").read_text())
        self.assertEqual(evaluation["excluded_rows"], 1)
        self.assertEqual(evaluation["status"], "pass")
        bad = dict(complete)
        bad["measurements"] = [dict(complete["measurements"][0]), {**complete["measurements"][1], "coupon_id": "CAL-1"}]
        measurements.write_bytes(canonical(bad))
        self.assertNotEqual(self.run_tool("evaluate", "--bundle", bundle, "--measurements", measurements,
                                          "--output", self.root / "bad-evaluation.json").returncode, 0)

    def test_templates_are_explicit(self):
        output = self.root / "templates"
        result = self.run_tool("templates", "--out", output)
        self.assertEqual(result.returncode, 0)
        self.assertIn("REPLACE_WITH_REAL_FINGERPRINT_HASH", (output / "experiment.template.json").read_text())


if __name__ == "__main__":
    unittest.main(verbosity=2)
