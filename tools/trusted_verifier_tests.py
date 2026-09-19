"""Independent verifier acceptance, adversarial mutations and modal replay tests."""
import argparse
from decimal import Decimal
import gzip
import hashlib
import json
from pathlib import Path
import re
import random
import subprocess
import sys
import tempfile
import unittest
import jsonschema

sys.dont_write_bytecode = True
from verifier_fixture_manifests import ROOT, manifest

parser = argparse.ArgumentParser()
parser.add_argument("verifier", type=Path)
parser.add_argument("--evidence", type=Path, required=True)
parser.add_argument("--production-replay", type=Path)
ARGS = parser.parse_args()
EXE = ARGS.verifier.resolve()
FINGERPRINT = (ROOT / "tests/verifier/machine.json").read_bytes()
GOLDENS = {p.name.removesuffix(".gcode.gz"): gzip.decompress(p.read_bytes())
           for p in sorted((ROOT / "tests/predictive/production").glob("*.gcode.gz"))}
EVIDENCE = {"starting_commit": "393904b9b92930058074019a5616097ec8ec2af8",
            "verifier_sha256": hashlib.sha256(EXE.read_bytes()).hexdigest(),
            "fingerprint_sha256": hashlib.sha256(FINGERPRINT).hexdigest(),
            "acceptance": [], "mutations": [], "modal_cases": []}
SCHEMAS = {name: jsonschema.Draft202012Validator(json.loads((ROOT / "docs/predictive-slicer/schema" / f"{name}.schema.json").read_text()))
           for name in ("verifier-machine", "verification-manifest", "verification-work-packet", "verifier-report")}


def encoded(data):
    return json.dumps(data, sort_keys=True, separators=(",", ":")).encode()


def work_packet(program, fingerprint, binding, overrides=None):
    packet = {"schema_version": "js-work-packet-1", "canonicalization": "js-canonical-json-1",
              "program_sha256": hashlib.sha256(program).hexdigest(),
              "fingerprint_sha256": hashlib.sha256(encoded(json.loads(fingerprint))).hexdigest(),
              "manifest_sha256": hashlib.sha256(encoded(binding)).hexdigest(),
              "compiler": {"commit": binding["compiler_commit"], "build_id": binding["compiler_build_id"]},
              "schema_versions": {"machine": "js-machine-1", "manifest": "js-verification-1", "packet": "js-work-packet-1", "report": "js-verifier-report-1"},
              "paths": [], "temporary_structures": [], "datums": []}
    if overrides:
        packet.update(overrides)
    return packet


def before_shutdown(program, text):
    marker = b"M104 S0"
    position = program.index(marker)
    line = program[:position].count(b"\n") + 1
    return program[:position] + text.encode() + program[position:], line


def short(body):
    return ("G21\nG90\nM83\nM109 S200\n" + body + "M104 S0\n; objects_info = {\"objects\":[]}\n; preflight_config = end\n").encode()


class VerifierTests(unittest.TestCase):
    def run_verify(self, program, fingerprint=FINGERPRINT, binding=None, packet=None):
        if binding is None:
            binding = manifest(program, fingerprint)
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            files = [folder / n for n in ("program.gcode", "machine.json", "manifest.json", "packet.json")]
            manifest_bytes = encoded(binding) if not isinstance(binding, bytes) else binding
            binding_obj = json.loads(manifest_bytes)
            packet_obj = packet if packet is not None else work_packet(program, fingerprint, binding_obj)
            packet_bytes = encoded(packet_obj) if not isinstance(packet_obj, bytes) else packet_obj
            payloads = [program, fingerprint, manifest_bytes, packet_bytes]
            for p, value in zip(files, payloads):
                p.write_bytes(value)
            result = subprocess.run([str(EXE), *map(str, files)], capture_output=True, timeout=20)
            self.assertIn(result.returncode, (0, 1), result.stderr.decode(errors="replace"))
            report = json.loads(result.stdout)
            SCHEMAS["verifier-report"].validate(report)
            self.assertEqual(result.returncode == 0, report["accepted"])
            expected_hashes = dict(zip(("program", "fingerprint", "manifest", "work_packet"),
                              (hashlib.sha256(value).hexdigest() for value in payloads)))
            expected_hashes["work_packet_canonical"] = hashlib.sha256(encoded(json.loads(packet_bytes))).hexdigest()
            self.assertEqual(report["input_sha256"], expected_hashes)
            self.assertEqual([p.read_bytes() for p in files], payloads, "verifier mutated input")
            self.assertEqual(sorted(p.name for p in folder.iterdir()), sorted(p.name for p in files), "verifier wrote a repair/output file")
            self.assertEqual(report["findings"], sorted(report["findings"], key=lambda f: (f["line"], f["code"], f["message"])))
            return report, result.stdout

    def assert_scanned(self, report, program):
        self.assertEqual(report["lines_scanned"], len(program.splitlines()))
        self.assertEqual(report["commands_seen"], sum(bool(line.partition(b";")[0].strip()) for line in program.splitlines()))

    def assert_rejected(self, program, code, binding=None, fingerprint=FINGERPRINT, expected_line=None):
        report, raw = self.run_verify(program, fingerprint, binding)
        self.assertFalse(report["accepted"], raw)
        self.assert_scanned(report, program)
        errors = [f for f in report["findings"] if f["severity"] == "error"]
        self.assertEqual({f["code"] for f in errors}, {code}, errors[:5])
        if expected_line is not None:
            self.assertTrue(any(f["line"] == expected_line for f in errors), errors)
        self.assertEqual(raw, self.run_verify(program, fingerprint, binding)[1])
        return errors

    def test_accepted_goldens(self):
        SCHEMAS["verifier-machine"].validate(json.loads(FINGERPRINT))
        self.assertEqual(len(GOLDENS), 7)
        for name, program in GOLDENS.items():
            with self.subTest(name=name):
                binding = json.loads((ROOT / "tests/verifier" / f"{name}.manifest.json").read_text())
                packet = json.loads((ROOT / "tests/verifier" / f"{name}.packet.json").read_text())
                SCHEMAS["verification-manifest"].validate(binding)
                SCHEMAS["verification-work-packet"].validate(packet)
                report, raw = self.run_verify(program, binding=binding, packet=packet)
                self.assertTrue(report["accepted"], raw.decode())
                self.assertTrue(report["replay_complete"])
                self.assert_scanned(report, program)
                self.assertEqual(raw, self.run_verify(program, binding=binding, packet=packet)[1])
                self.assertEqual(len(report["unproven_properties"]), 7)
                EVIDENCE["acceptance"].append({"fixture": name, "program_sha256": hashlib.sha256(program).hexdigest(),
                    "accepted": True, "deterministic": True, "report": report})

    def test_production_mutations(self):
        base = GOLDENS["mechanical"]
        cases = {
            "out_of_bounds": ("G1 X400 F60\n", "motion.bounds", 0),
            "tool_envelope": ("G1 X299.9 F60\n", "tool.envelope", 0),
            "feedrate": ("G1 F18000\n", "motion.feedrate", 0),
            "axis_velocity": ("M83\nG1 E-1 F9000\n", "motion.axis_velocity", 1),
            "volumetric_flow": ("M83\nG1 E1 F6000\n", "extrusion.flow", 1),
            "nan": ("G1 XNaN\n", "numeric.nonfinite", 0),
            "infinite": ("G1 Xinf\n", "numeric.nonfinite", 0),
            "cold_extrusion": ("M109 S100\nM83\nG1 E1 F60\n", "extrusion.cold", 2),
            "coordinate_mode_parameters": ("G91 X0\n", "modal.parameters", 0),
            "unsupported": ("M302 S0\n", "command.unsupported", 0),
            "invalid_tool": ("T9\n", "tool.invalid", 0),
            "temperature": ("M104 S999\n", "temperature.bounds", 0),
            "extrusion_discontinuity": ("M83\nG1 E201 F60\n", "extrusion.continuity", 1),
            "acceleration": ("M204 P10001\n", "motion.config_limit", 0),
            "arcs": ("G2 X10 Y10 I2 J2 F60\n", "arc.unsupported", 0),
            "retraction": ("M83\n" + "G1 E-199 F60\n" * 6, "extrusion.retraction", 6),
        }
        for name, (insertion, code, delta) in cases.items():
            with self.subTest(name=name):
                program, line = before_shutdown(base, insertion)
                errors = self.assert_rejected(program, code, expected_line=line + delta)
                EVIDENCE["mutations"].append({"name": name, "base": "mechanical", "insert": insertion,
                    "expected_code": code, "expected_line": line + delta, "errors": errors, "full_input_scanned": True})
        # Remove both declarations: positive E mode declarations cannot establish XYZ mode.
        program = re.sub(rb"(?m)^G90[^\n]*", b"; coordinate declaration removed", base)
        errors = self.assert_rejected(program, "modal.unknown")
        EVIDENCE["mutations"].append({"name": "missing_coordinate_mode", "expected_code": "modal.unknown", "error_count": len(errors), "full_input_scanned": True})
        binding = manifest(base, FINGERPRINT)
        binding["fingerprint_sha256"] = "0" * 64
        errors = self.assert_rejected(base, "binding.fingerprint", binding, expected_line=0)
        EVIDENCE["mutations"].append({"name": "fingerprint_hash", "expected_code": "binding.fingerprint", "errors": errors, "full_input_scanned": True})
        program = base[:base.rindex(b"; preflight_config = end")]
        errors = self.assert_rejected(program, "program.truncated", expected_line=len(program.splitlines()))
        EVIDENCE["mutations"].append({"name": "truncated", "expected_code": "program.truncated", "errors": errors, "full_input_scanned": True})
        base = GOLDENS["multi_object"]
        marker = b"; stop printing object multi_object-0 id:0 copy 0"
        location = base.index(marker, base.index(b"M104 S200"))
        program = base[:location] + base[location:].replace(marker, b"; stop printing object multi_object-1 id:1 copy 0", 1)
        binding = manifest(program, FINGERPRINT, [f"multi_object-{i} id:{i} copy 0" for i in range(2)])
        errors = self.assert_rejected(program, "object.boundary", binding, expected_line=base[:location].count(b"\n") + 1)
        EVIDENCE["mutations"].append({"name": "object_boundary", "base": "multi_object", "expected_code": "object.boundary", "errors": errors, "full_input_scanned": True})

    def test_original_220mm_bed_rejects_wipe_tower(self):
        fp = json.loads(FINGERPRINT)
        fp["build_max_mm"] = [220, 220, 220]
        fp["machine_id"] = "SYNTHETIC-220-PROFILE-BOUNDS"
        raw = encoded(fp)
        # This is an already unsafe program under a different machine, not an
        # isolated mutation. Later diagnostics after failed replay are untrusted.
        report, _ = self.run_verify(GOLDENS["multi_material"], fingerprint=raw)
        self.assertFalse(report["accepted"])
        self.assert_scanned(report, GOLDENS["multi_material"])
        errors = [f for f in report["findings"] if f["severity"] == "error"]
        self.assertEqual(errors[0]["code"], "motion.bounds")
        passed = []
        for name, program in GOLDENS.items():
            if name == "multi_material":
                continue
            objects = json.loads((ROOT / "tests/verifier" / f"{name}.manifest.json").read_text())["objects"]
            report, output = self.run_verify(program, fingerprint=raw, binding=manifest(program, raw, objects))
            self.assertTrue(report["accepted"], output.decode())
            passed.append(name)
        EVIDENCE["original_220mm_bed"] = {"multi_material_accepted": False, "error_count": len(errors),
                                          "first_error": errors[0], "accepted_fixtures": passed}

    def test_modal_replay(self):
        cases = [
            ("relative_absolute_reset", "G1 X20 Y20 Z1 F600\nG92 X0\nG1 X5\nG91\nG1 X-2 E-1\nM82\nG1 E0\nG90\nG1 X10\n", [30, 20, 1]),
            ("inches", "G20\nG91\nG1 X1 F60\nG21\nG1 X1\n", [36.4, 10, 0]),
            ("shared_heater_tool", "T1\nG1 E1 F60\nT0\nG1 E1\n", [10, 10, 0]),
            ("compact", "G1X20Y20Z1E.5F600\n", [20, 20, 1]),
            ("config_limits", "M201 X500\nM203 X10\nM204 P500 T600 R700\nG1 X20 F600\n", [20, 10, 0]),
            ("override_backup", "M220 B\nM220 S50\nG1 X20 F600\nM220 R\nG1 X30\n", [30, 10, 0]),
        ]
        for name, body, position in cases:
            with self.subTest(name=name):
                report, raw = self.run_verify(short(body))
                self.assertTrue(report["accepted"], raw.decode())
                for actual, expected in zip(report["final_state"]["position_mm"], position):
                    self.assertAlmostEqual(actual, expected)
                EVIDENCE["modal_cases"].append({"name": name, "final_state": report["final_state"]})

    def test_seeded_modal_differential(self):
        # A separate decimal replay using split words, not the C++ lexer or any
        # generator code. Exercise mode/reset permutations with bounded moves.
        rng = random.Random(1551)
        commands = ["G1 X20 Y20 Z1 F600"]
        for _ in range(120):
            commands += [rng.choice(["G20", "G21"]), rng.choice(["G90", "G91"])]
            if rng.random() < 0.7:
                commands += [rng.choice(["M82", "M83"])]
            # A local reset keeps both absolute and relative commands bounded;
            # the second move differs in physical distance between those modes.
            commands += ["G92 X0 E0", "G1 X.01 E.001 F60", "G1 X-.01 E.002"]
        position = [Decimal(10), Decimal(10), Decimal(0)]
        origin = [Decimal(0)] * 3
        unit, e, absolute, e_absolute = Decimal(1), Decimal(0), True, False
        for command in commands:
            words = command.split()
            code = words[0]
            values = {word[0]: Decimal(word[1:]) for word in words[1:]}
            if code in ("G20", "G21"):
                unit = Decimal("25.4") if code == "G20" else Decimal(1)
            elif code in ("G90", "G91"):
                absolute = e_absolute = code == "G90"
            elif code in ("M82", "M83"):
                e_absolute = code == "M82"
            elif code == "G92":
                for i, axis in enumerate("XYZ"):
                    if axis in values:
                        origin[i] = position[i] - values[axis] * unit
                if "E" in values:
                    e = values["E"] * unit
            elif code == "G1":
                for i, axis in enumerate("XYZ"):
                    if axis in values:
                        position[i] = values[axis] * unit + (origin[i] if absolute else position[i])
                if "E" in values:
                    e = values["E"] * unit + (0 if e_absolute else e)
        report, raw = self.run_verify(short("\n".join(commands) + "\n"))
        self.assertTrue(report["accepted"], raw.decode())
        for actual, expected in zip(report["final_state"]["position_mm"], position):
            self.assertAlmostEqual(actual, float(expected), places=10)
        self.assertAlmostEqual(report["final_state"]["e_mm"], float(e), places=10)
        for actual, expected in zip(report["final_state"]["coordinate_origin_mm"], origin):
            self.assertAlmostEqual(actual, float(expected), places=10)
        EVIDENCE["differential"] = {"seed": 1551, "commands": len(commands), "decimal_state_matches": True}

    def test_fan_temperature_and_limits(self):
        body = "M140 S50\nM190 R60\nM106 P0 S128\nG1 X20 F60\nM107\nM140 S0\n"
        report, raw = self.run_verify(short(body))
        self.assertTrue(report["accepted"], raw.decode())
        self.assertEqual(report["final_state"]["fan_pwm"], [0])
        self.assertEqual(report["final_state"]["bed_target_c"], 0)
        for instruction, code in (("M106 S256", "fan.range"), ("M106 P9", "fan.index"),
                                  ("M140 S121", "temperature.bounds"), ("M203 X201", "motion.config_limit"),
                                  ("M201 E10001", "motion.config_limit"), ("M220 R", "modal.speed_backup")):
            with self.subTest(instruction=instruction):
                self.assert_rejected(short("G1 X20 F60\n" + instruction + "\n"), code)
        self.assert_rejected(short("M203 X1\nG1 X20 F120\n"), "motion.axis_velocity")

    def test_g90_resets_extrusion_override(self):
        # M83: E=2; G90 clears that override; absolute E=3 means one more mm.
        report, raw = self.run_verify(short("G1 E2 F60\nG90\nG1 E3\n"))
        self.assertTrue(report["accepted"], raw.decode())
        self.assertEqual(report["final_state"]["e_mm"], 3)
        self.assertEqual(report["final_state"]["extrusion_mode"], "absolute")

    def test_syntax_and_metadata_mutations(self):
        for instruction, code in (("G1 X1 X2", "syntax.duplicate"), ("G1 X+", "syntax.number"),
                                  ("G1 X-Inf", "numeric.nonfinite"), ("G1 X1e3", "syntax.word"),
                                  ("G1 X10000001", "numeric.sanity"), ("G1 X1*42", "syntax.word"),
                                  ("G90 G91", "modal.parameters"), ("G1 X1\x00", "syntax.line"),
                                  ("; JS_FINGERPRINT_SHA256=" + "0" * 64, "binding.fingerprint")):
            with self.subTest(instruction=instruction):
                self.assert_rejected(short("G1 X20 F60\n" + instruction + "\n"), code)
        program = short("G1 X20 F60\n")
        broken = program.replace(b'{"objects":[]}', b'{"objects": [}')
        report, _ = self.run_verify(broken)
        errors = [f for f in report["findings"] if f["severity"] == "error"]
        self.assertEqual(errors[0]["code"], "syntax.metadata")
        self.assert_scanned(report, broken)

    def test_schema_constraints_and_offset_rejection(self):
        program = short("G1 X20 F60\n")
        for field, value in (("schema_version", "js-machine-2"), ("heaters", []),
                             ("axis_velocity_mm_s", [0, 200, 200, 120]), ("max_feedrate_mm_s", float("nan")),
                             ("tools", "invalid"), ("unexpected", 1)):
            with self.subTest(field=field):
                fp = json.loads(FINGERPRINT)
                fp[field] = value
                report, _ = self.run_verify(program, fingerprint=encoded(fp))
                self.assertFalse(report["accepted"])
                self.assertEqual({f["code"] for f in report["findings"]}, {"schema.machine"})
        fp = json.loads(FINGERPRINT)
        fp["tools"][1]["offset_mm"][0] = 1
        report, _ = self.run_verify(program, fingerprint=encoded(fp))
        self.assertEqual(report["findings"][0]["code"], "tool.offset_unsupported")

    def test_shutdown_and_object_declaration_mutations(self):
        base = GOLDENS["mechanical"]
        self.assert_rejected(base.replace(b"M104 S0\n", b"M104 S200\n"), "shutdown.heaters")
        base = GOLDENS["multi_object"]
        binding = json.loads((ROOT / "tests/verifier/multi_object.manifest.json").read_text())
        for program in (base.replace(b"; stop printing object multi_object-0 id:0 copy 0", b"; stop printing object UNKNOWN id:0 copy 0", 1),
                        base.replace(b"; printing object multi_object-0 id:0 copy 0", b"; declaration removed", 1)):
            binding["program_sha256"] = hashlib.sha256(program).hexdigest()
            report, _ = self.run_verify(program, binding=binding)
            self.assertFalse(report["accepted"])
            errors = [f for f in report["findings"] if f["severity"] == "error"]
            self.assertIn(errors[0]["code"], ("object.label", "object.boundary"))
            self.assert_scanned(report, program)
        binding = manifest(short("G1 X20 F60\n"), FINGERPRINT, ["a id:0 copy 0", "b id:00 copy 0"])
        report, _ = self.run_verify(short("G1 X20 F60\n"), binding=binding)
        self.assertEqual(report["findings"][0]["code"], "manifest.objects")

    def test_g92_does_not_hide_out_of_bounds(self):
        self.assert_rejected(short("G92 X-299\nG1 X0 F60\n"), "motion.bounds")

    def test_flow_override_is_applied_to_e_only_motion(self):
        self.assert_rejected(short("M221 S200\nG1 E1 F3000\n"), "extrusion.flow")

    def test_retraction_survives_g92(self):
        fp = json.loads(FINGERPRINT)
        fp["max_retraction_mm"] = 3
        self.assert_rejected(short("G1 E-2 F60\nG92 E0\nG1 E-2\n"), "extrusion.retraction", fingerprint=encoded(fp))

    def test_heating_command_is_not_wait(self):
        program = short("G1 E1 F60\n").replace(b"M109 S200", b"M104 S200")
        self.assert_rejected(program, "extrusion.cold")

    def test_pressure_advance_and_unknown_commands_fail_closed(self):
        for command in ("M900 K0.02", "G28", "G29", "M200 D1.75", "M205 X10", "M500", "M486 S0", "EXCLUDE_OBJECT_START NAME=x"):
            with self.subTest(command=command):
                self.assert_rejected(short(command + "\nG1 X20 F60\n"), "command.unsupported")

    def test_json_and_manifest_compatibility(self):
        program = short("G1 X20 F60\n")
        for field, value in (("schema_version", "js-verification-99"), ("additional", True), ("fingerprint_sha256", "bad")):
            binding = manifest(program, FINGERPRINT)
            binding[field] = value
            report, _ = self.run_verify(program, binding=binding)
            self.assertFalse(report["accepted"])
            self.assertEqual(report["findings"][0]["code"], "schema.manifest")
        duplicate = FINGERPRINT.replace(b'"schema_version": "js-machine-1",', b'"schema_version": "js-machine-1", "schema_version": "js-machine-1",')
        report, _ = self.run_verify(program, fingerprint=duplicate)
        self.assertEqual(report["findings"][0]["code"], "schema.machine")

    def test_unproven_required_property_fails(self):
        program = short("G1 X20 F60\n")
        for property_name in ("datum_protection", "swept_collision", "made_up_rule"):
            binding = manifest(program, FINGERPRINT)
            binding["required_properties"] += [property_name]
            self.assert_rejected(program, "rule.unproven", binding)

    def test_program_binding_and_trailing_commands(self):
        program = short("G1 X20 F60\n")
        binding = manifest(program, FINGERPRINT)
        binding["program_sha256"] = "0" * 64
        self.assert_rejected(program, "binding.program", binding)
        trailing = program + b"G1 X20\n"
        report, _ = self.run_verify(trailing)
        self.assertFalse(report["accepted"])
        self.assert_scanned(report, trailing)
        self.assertEqual({f["code"] for f in report["findings"] if f["severity"] == "error"},
                         {"program.trailing", "program.truncated"})

    def test_work_packet_content_binding_and_canonicalization(self):
        program = short("G1 X20 F60\n")
        binding = manifest(program, FINGERPRINT)
        packet = work_packet(program, FINGERPRINT, binding)
        report, _ = self.run_verify(program, binding=binding, packet=packet)
        self.assertTrue(report["accepted"])
        # Canonical JSON permits harmless whitespace and key-order changes in
        # the bound machine/manifest documents.
        pretty_manifest = json.dumps(binding, indent=4).encode() + b"\n"
        report, _ = self.run_verify(program, binding=pretty_manifest, packet=packet)
        self.assertTrue(report["accepted"])
        pretty_packet = json.dumps(packet, indent=4).encode() + b"\n"
        report, _ = self.run_verify(program, binding=binding, packet=pretty_packet)
        self.assertTrue(report["accepted"])
        for name, mutation, code in (
                ("compiler_identity", {"compiler": {"commit": "deadbeef", "build_id": binding["compiler_build_id"]}}, "binding.compiler"),
                ("schema_version", {"schema_versions": {"machine": "js-machine-2", "manifest": "js-verification-1", "packet": "js-work-packet-1", "report": "js-verifier-report-1"}}, "schema.work_packet"),
                ("manifest_content", None, "binding.manifest")):
            with self.subTest(name=name):
                if mutation is None:
                    changed = dict(binding); changed["compiler_build_id"] = "different-build"
                    report, _ = self.run_verify(program, binding=changed, packet=packet)
                else:
                    changed_packet = dict(packet); changed_packet.update(mutation)
                    report, _ = self.run_verify(program, binding=binding, packet=changed_packet)
                self.assertFalse(report["accepted"])
                self.assertIn(code, {f["code"] for f in report["findings"] if f["severity"] == "error"})
        changed_program = program.replace(b"X20", b"X21", 1)
        report, _ = self.run_verify(changed_program, binding=binding, packet=packet)
        self.assertFalse(report["accepted"])
        self.assertIn("binding.program", {f["code"] for f in report["findings"] if f["severity"] == "error"})
        changed_fp = json.loads(FINGERPRINT); changed_fp["machine_id"] = "substituted"
        report, _ = self.run_verify(program, fingerprint=encoded(changed_fp), binding=binding, packet=packet)
        self.assertFalse(report["accepted"])
        self.assertIn("binding.fingerprint", {f["code"] for f in report["findings"] if f["severity"] == "error"})
        packet_with_intent = dict(packet)
        packet_with_intent["intent_manifest"] = {"datum": "d1", "revision": 1}
        packet_with_intent["intent_sha256"] = hashlib.sha256(encoded(packet_with_intent["intent_manifest"])).hexdigest()
        report, _ = self.run_verify(program, binding=binding, packet=packet_with_intent)
        self.assertTrue(report["accepted"])
        packet_with_intent["intent_manifest"]["revision"] = 2
        report, _ = self.run_verify(program, binding=binding, packet=packet_with_intent)
        self.assertFalse(report["accepted"])
        self.assertIn("binding.component", {f["code"] for f in report["findings"] if f["severity"] == "error"})

    def test_path_precedence_and_datum_contracts(self):
        program = short("G1 X20 F60\nG1 X30 F60\n")
        binding = manifest(program, FINGERPRINT)
        lines = program.splitlines(keepends=True)
        command_lines = [line for line in lines if line.partition(b";")[0].strip()]
        first = command_lines[-3]  # first motion, immediately before second motion and shutdown
        second = command_lines[-2]
        packet = work_packet(program, FINGERPRINT, binding, {"paths": [
            {"id": "p1", "command_start": 5, "command_end": 5, "command_sha256": hashlib.sha256(first).hexdigest(), "predecessors": [], "tool": 0},
            {"id": "p2", "command_start": 6, "command_end": 6, "command_sha256": hashlib.sha256(second).hexdigest(), "predecessors": ["p1"], "tool": 0}],
            "temporary_structures": [], "datums": []})
        packet["thermal_schedule_proposal"] = {
            "schema_version": "js-thermal-schedule-1", "model_hash": "fnv1a64:test",
            "baseline_order": ["p1", "p2"], "proposed_order": ["p1", "p2"],
            "precedence": [{"path": "p1", "predecessors": []}, {"path": "p2", "predecessors": ["p1"]}]}
        report, raw = self.run_verify(program, binding=binding, packet=packet)
        self.assertTrue(report["accepted"], raw.decode())
        bad_thermal = dict(packet)
        bad_thermal["thermal_schedule_proposal"] = dict(packet["thermal_schedule_proposal"])
        bad_thermal["thermal_schedule_proposal"]["proposed_order"] = ["p2", "p1"]
        report, _ = self.run_verify(program, binding=binding, packet=bad_thermal)
        self.assertFalse(report["accepted"])
        self.assertIn("thermal.precedence", {f["code"] for f in report["findings"] if f["severity"] == "error"})
        bad = dict(packet); bad["paths"] = [dict(packet["paths"][0]), dict(packet["paths"][1])]
        bad["paths"][1]["predecessors"] = ["missing"]
        report, _ = self.run_verify(program, binding=binding, packet=bad)
        self.assertFalse(report["accepted"])
        self.assertIn("path.predecessor", {f["code"] for f in report["findings"] if f["severity"] == "error"})
        bad = dict(packet); bad["paths"] = [dict(packet["paths"][0])]
        bad["paths"][0]["command_sha256"] = "0" * 64
        report, _ = self.run_verify(program, binding=binding, packet=bad)
        self.assertFalse(report["accepted"])
        self.assertIn("path.digest", {f["code"] for f in report["findings"] if f["severity"] == "error"})
        bad = dict(packet); bad["paths"] = [dict(packet["paths"][0]), dict(packet["paths"][0])]
        report, _ = self.run_verify(program, binding=binding, packet=bad)
        self.assertFalse(report["accepted"])
        self.assertIn("path.duplicate_id", {f["code"] for f in report["findings"] if f["severity"] == "error"})
        bad = dict(packet); bad["temporary_structures"] = [{"id": "s", "create_command": 6, "last_use_command": 5}]
        report, _ = self.run_verify(program, binding=binding, packet=bad)
        self.assertFalse(report["accepted"])
        self.assertIn("temporary.lifetime", {f["code"] for f in report["findings"] if f["severity"] == "error"})

        # A datum with a generous permitted envelope accepts the deposited
        # segment; narrowing it forces the conservative bead-sweep rejection.
        datum = {"id": "d1", "object": "datum-0 id:0 copy 0", "protected_min_mm": [19.5, 9.5, 0],
                 "protected_max_mm": [20, 10.5, 0.5], "permitted_min_mm": [-100, -100, -100], "permitted_max_mm": [100, 100, 100], "max_bead_radius_mm": 0.2}
        labeled = (b"; printing object datum-0 id:0 copy 0\n; stop printing object datum-0 id:0 copy 0\n" +
                   short("; printing object datum-0 id:0 copy 0\nG1 X20 E1 F60\n; stop printing object datum-0 id:0 copy 0\n").replace(b'; objects_info = {"objects":[]}', b'; objects_info = {"objects":[{"name":"datum-0 id:0 copy 0"}]}'))
        # Keep this focused on packet-side geometry; a body with the expected
        # identity is accepted and a disjoint permitted box is rejected.
        lb = manifest(labeled, FINGERPRINT, ["datum-0 id:0 copy 0"])
        packet = work_packet(labeled, FINGERPRINT, lb, {"datums": [datum]})
        report, _ = self.run_verify(labeled, binding=lb, packet=packet)
        self.assertTrue(report["accepted"])
        datum["permitted_min_mm"] = [19, 9, 0]
        datum["permitted_max_mm"] = [20.1, 11, 1]
        packet = work_packet(labeled, FINGERPRINT, lb, {"datums": [datum]})
        report, _ = self.run_verify(labeled, binding=lb, packet=packet)
        self.assertFalse(report["accepted"])
        self.assertIn("datum.envelope", {f["code"] for f in report["findings"] if f["severity"] == "error"})

    def test_parser_reports_a_later_independent_fault(self):
        program, line = before_shutdown(GOLDENS["mechanical"], "M302 S0\nT9\n")
        report, _ = self.run_verify(program)
        self.assertFalse(report["accepted"])
        self.assert_scanned(report, program)
        errors = [f for f in report["findings"] if f["severity"] == "error"]
        self.assertEqual([(f["line"], f["code"]) for f in errors],
                         [(line, "command.unsupported"), (line + 1, "tool.invalid")])
        EVIDENCE["continued_parsing"] = {"first_error_line": line, "later_tool_error_line": line + 1,
                                         "both_faults_identified": True}

    def test_independent_link_contract(self):
        contracts = list(EXE.parent.glob("verifier-link-contract.txt")) + list(EXE.parent.parent.glob("verifier-link-contract.txt"))
        self.assertTrue(contracts)
        text = contracts[0].read_text()
        self.assertIn("nlohmann_json::nlohmann_json", text)
        self.assertIn("executable=jslice_trusted_verifier", text)
        for forbidden in ("libslic3r", "jslice_predictive_core", "CanonicalGCode", "CompilerPasses"):
            self.assertNotIn(forbidden, text)
        EVIDENCE["link_contract"] = text

    @unittest.skipUnless(ARGS.production_replay, "production replay directory is optional")
    def test_analysis_disabled_enabled_equivalence(self):
        EVIDENCE["off_on"] = []
        for name in GOLDENS:
            binding = json.loads((ROOT / "tests/verifier" / f"{name}.manifest.json").read_text())
            reports = []
            for mode in ("baseline", "ordinary", "analysis", "repeat"):
                program = (ARGS.production_replay / name / f"{mode}.gcode").read_bytes()
                binding["program_sha256"] = hashlib.sha256(program).hexdigest()
                report, raw = self.run_verify(program, binding=binding)
                self.assertTrue(report["accepted"], raw.decode())
                del report["input_sha256"] # exact input/manifest hashes differ only with generation time
                reports.append(report)
            self.assertTrue(all(r == reports[0] for r in reports))
            EVIDENCE["off_on"].append({"fixture": name, "four_results_equivalent": True})


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(VerifierTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    EVIDENCE["tests_run"] = result.testsRun
    EVIDENCE["successful"] = result.wasSuccessful()
    EVIDENCE["skipped"] = len(result.skipped)
    ARGS.evidence.parent.mkdir(parents=True, exist_ok=True)
    ARGS.evidence.write_text(json.dumps(EVIDENCE, indent=2) + "\n", encoding="utf-8", newline="\n")
    sys.exit(0 if result.wasSuccessful() else 1)
