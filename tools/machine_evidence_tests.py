#!/usr/bin/env python3
"""Adversarial tests for the AD5M/AD5X machine-evidence bootstrap."""
from __future__ import annotations

import copy
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "machine_evidence.py"
SCHEMA = ROOT / "docs" / "predictive-slicer" / "schema" / "machine-evidence.schema.json"


def load_tool():
    spec = importlib.util.spec_from_file_location("machine_evidence", TOOL)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class MachineEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.tool = load_tool()

    def tearDown(self):
        self.temp.cleanup()

    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True, text=True, check=False)

    def test_deterministic_output_and_schema(self):
        a = self.root / "a.json"
        b = self.root / "b.json"
        self.assertEqual(self.run_cli("bootstrap", "--model", "AD5M", "--output", a).returncode, 0)
        self.assertEqual(self.run_cli("bootstrap", "--model", "AD5M", "--output", b).returncode, 0)
        self.assertEqual(a.read_bytes(), b.read_bytes())
        jsonschema.Draft202012Validator(json.loads(SCHEMA.read_text(encoding="utf-8"))).validate(json.loads(a.read_text(encoding="utf-8")))

    def test_provenance_and_conflicts_are_retained(self):
        artifact = self.tool.build_artifact("AD5M")
        for fact in artifact["published_facts"]:
            self.assertIn(fact["source"]["class"], {"manufacturer", "slicer_profile", "community_firmware"})
            self.assertTrue(fact["source"]["artifact"])
            self.assertIn("observation_date", fact["source"])
            self.assertIn("qualification_state", fact)
        subjects = {row["subject"] for row in artifact["conflicts"]}
        self.assertTrue({"coordinate_convention", "published_vs_qualified_limits", "thermal_limit_vs_firmware_ceiling", "startup_behavior"} <= subjects)
        self.assertEqual(artifact["qualification"]["qualification_state"], "unqualified")

    def test_ad5m_and_ad5x_coordinate_conventions_are_distinct(self):
        ad5m = self.tool.build_artifact("AD5M")
        ad5x = self.tool.build_artifact("AD5X")
        def get(artifact, identifier):
            return next(row["value"] for row in artifact["published_facts"] if row["id"] == identifier)
        self.assertEqual(get(ad5m, "printable_xy"), [-110, 110, -110, 110])
        self.assertEqual(get(ad5x, "printable_xy"), [0, 220, 0, 220])
        self.assertNotEqual(get(ad5m, "hotend_descriptor"), None)
        self.assertEqual(next(row for row in ad5x["conflicts"] if row["subject"] == "hotend_geometry")["values"][1]["value"],
                         "AD5X nozzle design differs; exact geometry not inherited")

    def complete_observations(self, model="AD5M"):
        tool_change = ({"applicability": "required", "artifact_identity": "exports/tool-change.gcode",
                        "sha256": "d" * 64} if model == "AD5X" else
                       {"applicability": "not_applicable", "reason": self.tool.AD5M_TOOL_CHANGE_REASON})
        return {"source_class": "measured", "artifact_identity": f"physical-capture/{model}-serial-actual", "revision": "capture-1",
                "observation_date": "2026-09-19", "fields": {
                    "serial": "REAL-SERIAL-CAPTURED",
                    "firmware_identity": "stock Flashforge firmware",
                    "firmware_version": "REAL-VERSION-CAPTURED",
                    "nozzle_identifier": "REAL-NOZZLE-CAPTURED",
                    "nozzle_diameter_mm": 0.4,
                    "slicer_profile_identity": "OrcaSlicer AD5M pinned profile",
                    "slicer_profile_hash": "a" * 64,
                    "machine_artifact_identity": "machine/config-export.json",
                    "machine_artifact_sha256": "b" * 64,
                    "sequence_evidence": {
                        "start": {"applicability": "required", "artifact_identity": "exports/start.gcode", "sha256": "c" * 64},
                        "end": {"applicability": "required", "artifact_identity": "exports/end.gcode", "sha256": "e" * 64},
                        "tool_change": tool_change,
                    },
                    "material_manufacturer": "REAL-MANUFACTURER-CAPTURED",
                    "material_type": "PLA",
                    "material_color": "REAL-COLOR-CAPTURED",
                    "material_lot": "REAL-LOT-CAPTURED",
                    "ambient_temperature_c": 23.5,
                    "operator": "REAL-OPERATOR-CAPTURED",
                    "observation_date": "2026-09-19",
                    "coordinate_convention": {"name": "captured actual convention", "origin": "REAL-CAPTURED", "units": "mm"},
                }}

    def test_missing_identity_firmware_startup_and_ambiguous_coordinates_fail_closed(self):
        observations = {"source_class": "user_supplied", "artifact_identity": "capture-note", "fields": {
            "coordinate_convention": {"name": "ambiguous", "ambiguous": True},
            "firmware_version": "unknown",
        }}
        output = self.root / "draft.json"
        self.assertEqual(self.run_cli("bootstrap", "--model", "AD5M", "--observations", self.write_json(observations), "--output", output).returncode, 0)
        result = self.run_cli("validate", "--input", output, "--require-review-ready")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("serial", result.stderr)
        self.assertIn("coordinate_convention", result.stderr)
        self.assertIn("firmware_version", result.stderr)
        self.assertIn("sequence_evidence.end", result.stderr)

    def test_complete_observations_only_become_review_ready_not_qualified(self):
        output = self.root / "observed.json"
        observations = self.write_json(self.complete_observations("AD5X"))
        self.assertEqual(self.run_cli("bootstrap", "--model", "AD5X", "--observations", observations, "--output", output, "--require-review-ready").returncode, 0)
        artifact = json.loads(output.read_text(encoding="utf-8"))
        jsonschema.Draft202012Validator(json.loads(SCHEMA.read_text(encoding="utf-8"))).validate(artifact)
        self.assertTrue(artifact["qualification"]["coupon_bundle_eligible_for_review"])
        self.assertFalse(artifact["qualification"]["physical_qualification_claim"])
        self.assertEqual(artifact["machine"]["serial"], "REAL-SERIAL-CAPTURED")

    def test_review_ready_ad5m_uses_explicit_not_applicable_tool_change(self):
        output = self.root / "ad5m-observed.json"
        observations = self.write_json(self.complete_observations("AD5M"))
        result = self.run_cli("bootstrap", "--model", "AD5M", "--observations", observations,
                              "--output", output, "--require-review-ready")
        self.assertEqual(result.returncode, 0, result.stderr)
        artifact = json.loads(output.read_text(encoding="utf-8"))
        tool_change = artifact["sequence_evidence"]["tool_change"]
        self.assertEqual(tool_change["applicability"], "not_applicable")
        self.assertIn("single-material", tool_change["reason"])
        self.assertNotIn("sha256", tool_change)
        self.assertFalse(artifact["qualification"]["physical_qualification_claim"])

    def test_ad5m_rejects_fabricated_tool_change_hash(self):
        observations = self.complete_observations("AD5M")
        observations["fields"]["sequence_evidence"]["tool_change"] = {
            "applicability": "required", "artifact_identity": "invented-na.gcode", "sha256": "f" * 64}
        result = self.run_cli("bootstrap", "--model", "AD5M", "--observations", self.write_json(observations),
                              "--output", self.root / "fabricated.json")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("applicability=not_applicable", result.stderr)

    def test_ad5x_requires_present_tool_change_evidence(self):
        for value in (None, {"applicability": "not_applicable", "reason": "omitted"}):
            with self.subTest(value=value):
                observations = self.complete_observations("AD5X")
                if value is None:
                    del observations["fields"]["sequence_evidence"]["tool_change"]
                else:
                    observations["fields"]["sequence_evidence"]["tool_change"] = value
                result = self.run_cli("bootstrap", "--model", "AD5X", "--observations", self.write_json(observations),
                                      "--output", self.root / f"missing-{len(list(self.root.glob('missing-*.json')))}.json",
                                      "--require-review-ready")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("tool_change", result.stderr)

    def test_inferred_collision_geometry_cannot_be_exact_proof(self):
        artifact = self.tool.build_artifact("AD5M")
        artifact["collision_geometry"] = {
            "coordinate_frame": "nozzle-axis", "units": "mm", "nozzle_tip_origin": [0, 0, 0],
            "mesh_hash": "f" * 64, "transform": {"matrix": "identity"},
            "fixed_components": [], "moving_components": [], "applicable_printer_nozzle_state": "draft",
            "uncertainty_inflation_margin": 2.0, "provenance": {"class": "user_supplied", "artifact": "image", "revision": None, "observation_date": "2026-09-19", "qualification_state": "unqualified", "uncertainty": "inferred"},
            "qualification_state": "unqualified",
        }
        path = self.root / "geometry.json"
        path.write_bytes(self.tool.canonical(artifact))
        result = self.run_cli("validate", "--input", path, "--exact-collision-proof")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("collision", result.stderr.lower())

    def test_source_hash_corruption_is_rejected(self):
        artifact = self.tool.build_artifact("AD5X")
        artifact["published_facts"][0]["value"] = [999, 999, 999]
        path = self.root / "corrupt.json"
        path.write_bytes(self.tool.canonical(artifact))
        result = self.run_cli("validate", "--input", path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("integrity", result.stderr)

    def test_profile_hash_corruption_and_uncalibrated_limits_are_explicit(self):
        observations = self.complete_observations()
        observations["fields"]["slicer_profile_text"] = "profile bytes"
        observations["fields"]["slicer_profile_hash"] = "b" * 64
        result = self.run_cli("bootstrap", "--model", "AD5M", "--observations", self.write_json(observations), "--output", self.root / "bad.json")
        self.assertNotEqual(result.returncode, 0)
        artifact = self.tool.build_artifact("AD5M")
        limits = next(row for row in artifact["published_facts"] if row["id"] == "maximum_nozzle_temperature")
        pla = next(row for row in artifact["published_facts"] if row["id"] == "generic_pla_seed")
        self.assertEqual(limits["qualification_state"], "published")
        self.assertEqual(pla["qualification_state"], "unqualified")
        self.assertIn("uncalibrated", pla["uncertainty"])

    def test_malformed_hashes_are_rejected(self):
        mutations = (
            ("slicer_profile_hash", "abc"),
            ("machine_artifact_sha256", "A" * 64),
        )
        for field, value in mutations:
            with self.subTest(field=field):
                observations = self.complete_observations("AD5M")
                observations["fields"][field] = value
                result = self.run_cli("bootstrap", "--model", "AD5M", "--observations", self.write_json(observations),
                                      "--output", self.root / f"bad-{field}.json", "--require-review-ready")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(field, result.stderr)
        observations = self.complete_observations("AD5X")
        observations["fields"]["sequence_evidence"]["start"]["sha256"] = "short"
        result = self.run_cli("bootstrap", "--model", "AD5X", "--observations", self.write_json(observations),
                              "--output", self.root / "bad-sequence.json")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("sequence_evidence.start.sha256", result.stderr)
        artifact = self.tool.build_artifact("AD5M")
        artifact["collision_geometry"] = {
            "coordinate_frame": "nozzle-axis", "units": "mm", "nozzle_tip_origin": [0, 0, 0],
            "mesh_hash": "short", "transform": {"matrix": "identity"}, "fixed_components": [],
            "moving_components": [], "applicable_printer_nozzle_state": "draft",
            "uncertainty_inflation_margin": 2.0, "provenance": {"class": "user_supplied"},
            "qualification_state": "unqualified",
        }
        path = self.root / "bad-mesh.json"
        path.write_bytes(self.tool.canonical(artifact))
        result = self.run_cli("validate", "--input", path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("mesh_hash", result.stderr)

    def test_printer_specific_observations_templates_are_deterministic(self):
        outputs = []
        for model in ("AD5M", "AD5X"):
            first = self.root / f"{model}-1.json"
            second = self.root / f"{model}-2.json"
            self.assertEqual(self.run_cli("observations-template", "--model", model, "--output", first).returncode, 0)
            self.assertEqual(self.run_cli("observations-template", "--model", model, "--output", second).returncode, 0)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            outputs.append(json.loads(first.read_text(encoding="utf-8")))
        ad5m_tool = outputs[0]["fields"]["sequence_evidence"]["tool_change"]
        ad5x_tool = outputs[1]["fields"]["sequence_evidence"]["tool_change"]
        self.assertEqual(ad5m_tool["applicability"], "not_applicable")
        self.assertNotIn("sha256", ad5m_tool)
        self.assertEqual(ad5x_tool, {"applicability": "required", "artifact_identity": None, "sha256": None})

    def test_worksheets_are_printer_specific(self):
        ad5m = self.root / "AD5M-worksheet.md"
        ad5x = self.root / "AD5X-worksheet.md"
        self.assertEqual(self.run_cli("worksheet", "--model", "AD5M", "--output", ad5m).returncode, 0)
        self.assertEqual(self.run_cli("worksheet", "--model", "AD5X", "--output", ad5x).returncode, 0)
        self.assertNotIn("cutter", ad5m.read_text(encoding="utf-8").lower())
        self.assertIn("cutter", ad5x.read_text(encoding="utf-8").lower())

    def write_json(self, value):
        path = self.root / f"input-{len(list(self.root.glob('input-*.json')))}.json"
        path.write_bytes(self.tool.canonical(value))
        return path


if __name__ == "__main__":
    unittest.main(verbosity=2)
