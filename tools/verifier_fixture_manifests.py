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
            "initial_state": {"position_mm": [10, 10, 0], "e_mm": 0, "tool": 0,
                              "heater_temperature_c": [20], "bed_temperature_c": 20, "fan_pwm": [0]},
            "objects": list(objects), "required_properties": PROPERTIES,
            "end_marker": "; preflight_config = end"}


if __name__ == "__main__":
    fingerprint = (ROOT / "tests/verifier/machine.json").read_bytes()
    for source in sorted((ROOT / "tests/predictive/production").glob("*.gcode.gz")):
        name = source.name.removesuffix(".gcode.gz")
        objects = [f"multi_object-{i} id:{i} copy 0" for i in range(2)] if name == "multi_object" else []
        data = manifest(gzip.decompress(source.read_bytes()), fingerprint, objects)
        (ROOT / "tests/verifier" / f"{name}.manifest.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")
