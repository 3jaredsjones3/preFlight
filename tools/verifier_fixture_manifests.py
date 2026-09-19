"""Explicitly generate trusted synthetic test manifests, never machine calibration."""
import gzip
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROPERTIES = ["syntax", "modal_state", "numeric", "build_volume", "tool_envelope",
              "axis_velocity", "configured_motion_limits", "volumetric_flow",
              "temperature_commands", "cold_extrusion", "extrusion_continuity",
              "object_boundaries", "fingerprint_binding", "program_integrity", "schema_compatibility"]


def manifest(program, fingerprint, objects=()):
    # An explicit trusted test startup. M1 goldens contain no homing command.
    return {"schema_version": "js-verification-1",
            "fingerprint_sha256": hashlib.sha256(fingerprint).hexdigest(),
            "program_sha256": hashlib.sha256(program).hexdigest(),
            "compiler_commit": "393904b9b92930058074019a5616097ec8ec2af8",
            "compiler_build_id": "jslice-m1.5-verifier-fixtures",
            "initial_state": {"position_mm": [10, 10, 0], "e_mm": 0, "tool": 0,
                              "heater_temperature_c": [20], "bed_temperature_c": 20, "fan_pwm": [0]},
            "objects": list(objects), "required_properties": list(PROPERTIES),
            "end_marker": "; preflight_config = end"}


def work_packet(program, fingerprint, binding):
    return {"schema_version": "js-work-packet-1", "canonicalization": "js-canonical-json-1",
            "program_sha256": hashlib.sha256(program).hexdigest(),
            "fingerprint_sha256": hashlib.sha256(json.dumps(json.loads(fingerprint), sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
            "manifest_sha256": hashlib.sha256(json.dumps(binding, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
            "compiler": {"commit": binding["compiler_commit"], "build_id": binding["compiler_build_id"]},
            "schema_versions": {"machine": "js-machine-1", "manifest": "js-verification-1", "packet": "js-work-packet-1", "report": "js-verifier-report-1"},
            "paths": [], "temporary_structures": [], "datums": []}


if __name__ == "__main__":
    fingerprint = (ROOT / "tests/verifier/machine.json").read_bytes()
    for source in sorted((ROOT / "tests/predictive/production").glob("*.gcode.gz")):
        name = source.name.removesuffix(".gcode.gz")
        objects = [f"multi_object-{i} id:{i} copy 0" for i in range(2)] if name == "multi_object" else []
        data = manifest(gzip.decompress(source.read_bytes()), fingerprint, objects)
        (ROOT / "tests/verifier" / f"{name}.manifest.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")
        packet = work_packet(gzip.decompress(source.read_bytes()), fingerprint, data)
        (ROOT / "tests/verifier" / f"{name}.packet.json").write_text(json.dumps(packet, indent=2) + "\n", encoding="utf-8", newline="\n")
