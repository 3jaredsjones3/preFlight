#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import jsonschema


def main() -> int:
    executable = Path(sys.argv[1])
    schema_path = Path(sys.argv[2])
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "proposal.json"
        first = subprocess.run([str(executable), str(output)], capture_output=True, text=True)
        assert first.returncode == 0, first.stderr
        payload = json.loads(output.read_text(encoding="utf-8"))
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
        jsonschema.Draft202012Validator(schema).validate(payload)
        first_bytes = output.read_bytes()
        second = subprocess.run([str(executable), str(output)], capture_output=True, text=True)
        assert second.returncode == 0, second.stderr
        assert output.read_bytes() == first_bytes
    print("Thermal schedule JSON schema and deterministic serialization passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
