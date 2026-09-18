"""M1 golden checks. No baseline is updated implicitly; subprocesses use argv lists."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate_artifact(path):
    def reject_constant(value):
        raise ValueError(f"Non-finite JSON: {value}")
    data = json.loads(path.read_text(encoding="utf-8"), parse_constant=reject_constant)
    require(data["schema_version"] == "m1-analysis-1", "wrong schema version")
    require(data["mode"] == "analysis_only", "printer-changing mode")
    require(data["order"] == "source_storage", "unexpected schedule claim")
    require(data["prediction"] == {"duration_s": None, "thermal": None, "bond": None}, "invented prediction")
    require(data["certification"]["status"] == "analysis_only", "false certification")
    require(data["certification"]["source_complete"], "incomplete source capture")
    require(bool(data["unmodeled_downstream_passes"]), "missing downstream limitations")
    entities = data["source_entities"]
    paths = data["bead_graph"]
    require([e["id"] for e in entities] == list(range(1, len(entities) + 1)), "unstable source IDs")
    require([p["id"] for p in paths] == list(range(1, len(paths) + 1)), "unstable path IDs")
    seen_children = {}
    for e in entities:
        parent = e["parent"]
        require(parent is None or 0 < parent < e["id"], "invalid parent")
        require(e["child_index"] == seen_children.get(parent, 0), "child order changed")
        seen_children[parent] = e["child_index"] + 1
        require(0 <= e["attributes"]["role_bits"] <= 65535, "role bits out of range")
        if e["bead_path"] is None:
            continue
        p = paths[e["bead_path"] - 1]
        require(p["source_entity"] == e["id"], "provenance not bijective")
        require(len(p["samples"]) == len(e["points_scaled"]), "source points discarded")
        c, a = e["context"], e["attributes"]
        for i, (point, sample) in enumerate(zip(e["points_scaled"], p["samples"])):
            xyz = [(point[0] + c["shift_scaled"][0]) * c["coordinate_scale_mm"],
                   (point[1] + c["shift_scaled"][1]) * c["coordinate_scale_mm"], c["print_z_mm"] if c["print_z_mm"] is not None else 0]
            require(sample[:3] == xyz, "coordinate conversion differs")
            require(sample[3:5] == [a["width_mm"], a["height_mm"]], "flow dimensions lost")
            volume = 0 if i == 0 else math.dist(p["samples"][i - 1][:3], xyz) * a["mm3_per_mm"]
            require(math.isclose(sample[5], volume, rel_tol=1e-12, abs_tol=1e-14), "volume conservation failed")
    require(sum(e["bead_path"] is not None for e in entities) == len(paths), "orphan bead paths")
    return data


def run_job(args):
    manifest_path = args.manifest.resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    validate_jobs(manifest_path)
    required = {"mechanical", "thin_wall", "bridge_support", "multi_object", "serpentine", "interlocking", "multi_material"}
    require({job["name"] for job in manifest["jobs"]} == required, "job suite must contain all seven roadmap cases")
    require(len(manifest["jobs"]) == 7, "duplicate jobs")
    # A fresh directory prevents stale G-code/reports from masquerading as success.
    args.output.mkdir(parents=True, exist_ok=False)
    evidence = []
    for job in manifest["jobs"]:
        folder = args.output / job["name"]
        folder.mkdir()
        require(bool(job["models"]) and bool(job["profiles"]), "models and pinned profiles required")
        inputs = [manifest_path.parent / item for item in job["profiles"] + job["models"]]
        hashes = {str(p.relative_to(manifest_path.parent)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
        shared = []
        for profile in job["profiles"]:
            shared += ["--load", str(manifest_path.parent / profile)]
        shared += job.get("args", [])
        require(not any(v.startswith(("--output", "--predictive-analysis", "--export-gcode")) for v in shared), "runner owns output and export options")
        shared += [str(manifest_path.parent / p) for p in job["models"]]
        outputs, commands = [], []
        # Compare untouched upstream to new ordinary mode AND new analysis mode.
        for label, binary, action in [("baseline", args.baseline, "--export-gcode"),
                                      ("ordinary", args.candidate, "--export-gcode"),
                                      ("analysis", args.candidate, "--predictive-analysis"),
                                      ("repeat", args.candidate, "--predictive-analysis")]:
            gcode = (folder / f"{label}.gcode").resolve()
            command = [str(binary.resolve()), action, "--output", str(gcode), *shared]
            commands.append(command)
            with (folder / f"{label}.log").open("wb") as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            require(gcode.is_file(), f"slicer did not create {gcode}")
            outputs.append(gcode)
        checks = []
        for candidate in outputs[1:]:
            command = [str(args.comparator.resolve()), str(outputs[0]), str(candidate), *manifest.get("volatile_comment_prefixes", [])]
            comparison = subprocess.run(command, capture_output=True, text=True, check=True)
            checks.append(comparison.stdout.strip())
        report = Path(str(outputs[2]) + ".artifact.json")
        repeat = Path(str(outputs[3]) + ".artifact.json")
        data = validate_artifact(report)
        require(report.read_bytes() == repeat.read_bytes(), "analysis report is not deterministic")
        require(bool(data["bead_graph"]), "empty analysis")
        # Expectations establish that each sliced job actually exercised its intended feature.
        expectations = job["expect"]
        for bits in expectations.get("role_bits", []):
            require(any(e["attributes"]["role_bits"] == bits for e in data["source_entities"] if e["bead_path"]), f"missing expected role {bits}")
        for key in ("object", "configured_tool", "generator"):
            for value in expectations.get(key, []):
                require(any(e["context"][key] == value for e in data["source_entities"] if e["bead_path"]), f"missing expected {key}={value}")
        evidence.append({"job": job["name"], "inputs_sha256": hashes, "commands": commands, "comparisons": checks,
                         "artifact_sha256": hashlib.sha256(report.read_bytes()).hexdigest()})
        print(f"PASS {job['name']}", flush=True)
    result = {"executables_sha256": {key: hashlib.sha256(getattr(args, key).read_bytes()).hexdigest()
                                    for key in ("baseline", "candidate", "comparator")}, "jobs": evidence}
    (args.output / "evidence.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


def validate_jobs(manifest_path):
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for job in manifest["jobs"]:
        require(bool(job["expect"]), "job needs feature expectations")
        for profile in job["profiles"]:
            require((manifest_path.parent / profile).is_file(), "profile missing")
        for model in job["models"]:
            root = ET.parse(manifest_path.parent / model).getroot()
            require(root.tag == "amf" and root.attrib["unit"] == "millimeter", "unexpected geometry format")
            for obj in root.findall("object"):
                points = [[float(v.findtext(f"coordinates/{axis}")) for axis in "xyz"]
                          for v in obj.findall("mesh/vertices/vertex")]
                require(bool(points), "empty AMF object")
                for volume in obj.findall("mesh/volume"):
                    require(bool(volume.findall("triangle")), "empty AMF volume")
                    for triangle in volume.findall("triangle"):
                        ids = [int(triangle.findtext(f"v{i}")) for i in (1, 2, 3)]
                        require(len(set(ids)) == 3 and all(0 <= i < len(points) for i in ids), "invalid AMF triangle")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    validate = commands.add_parser("validate")
    validate.add_argument("directory", type=Path)
    validate.add_argument("--schema", type=Path)
    jobs = commands.add_parser("validate-jobs")
    jobs.add_argument("manifest", type=Path)
    run = commands.add_parser("run")
    for name in ("manifest", "baseline", "candidate", "comparator", "output"):
        run.add_argument(f"--{name}", required=True, type=Path)
    args = parser.parse_args()
    if args.command == "validate":
        files = sorted(args.directory.glob("*.artifact.json"))
        require(len(files) == 7, "expected seven entity goldens")
        for file in files:
            data = validate_artifact(file)
            if args.schema:
                import jsonschema
                jsonschema.Draft202012Validator(json.loads(args.schema.read_text(encoding="utf-8"))).validate(data)
        print("Seven JSON goldens: provenance, geometry and volume checks passed")
    elif args.command == "validate-jobs":
        validate_jobs(args.manifest)
        print("AMF job geometry, profiles and expectations validated (slicing not run)")
    else:
        run_job(args)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
