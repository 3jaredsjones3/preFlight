#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import jsonschema


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    tool = root / "tools" / "thermal_calibration.py"
    fixture = root / "tests" / "predictive" / "thermal_calibration.json"
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "fit.json"
        first = subprocess.run([sys.executable, str(tool), str(fixture), str(output)],
                               capture_output=True, text=True)
        assert first.returncode == 0, first.stderr
        result = json.loads(output.read_text(encoding="utf-8"))
        schema = json.loads((root / "docs/predictive-slicer/schema/thermal-model-fit.schema.json").read_text(encoding="utf-8"))
        jsonschema.Draft202012Validator(schema).validate(result)
        assert result["schema_version"] == "js-thermal-model-fit-1"
        assert result["provenance"] == "measured" and result["calibrated"] is True
        assert result["fit_sample_count"] == 4
        assert result["cooling_time_constant_s"] > 0
        bad = Path(directory) / "bad.json"
        data = json.loads(fixture.read_text(encoding="utf-8"))
        data["measurements"] = data["measurements"][:2]
        bad.write_text(json.dumps(data), encoding="utf-8")
        rejected = subprocess.run([sys.executable, str(tool), str(bad), str(output)],
                                   capture_output=True, text=True)
        assert rejected.returncode != 0 and "at least three" in rejected.stderr
    print("Thermal calibration schema, fit, provenance and incomplete-data rejection passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
