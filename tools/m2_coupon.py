#!/usr/bin/env python3
"""Fail-closed M2 AD5M/AD5X coupon package tool.

This is deliberately a research-only packet tool.  It does not slice, lower,
or modify ordinary exports.  ``prepare`` accepts only a bound work packet and
the scheduler proposal that it carries, then moves complete, already-emitted
path blocks.  All physical eligibility checks happen before any printable
bundle is created.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


SPEC_VERSION = "js-m2-coupon-experiment-1"
PAIR_VERSION = "js-m2-coupon-pair-1"
PACKET_VERSION = "js-work-packet-1"
FORBIDDEN_MARKERS = ("synthetic", "inferred", "uncalibrated", "unknown", "tbd", "template", "incomplete")


class CouponError(ValueError):
    pass


def fail(message: str) -> None:
    raise CouponError(message)


def canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail(f"cannot read JSON {path}: {exc}")
    if not isinstance(value, dict):
        fail(f"JSON root must be an object: {path}")
    return value


def write_json(path: Path, value: Any) -> None:
    path.write_bytes(canonical(value))


def require(value: Any, message: str) -> None:
    if not value:
        fail(message)


def required_object(value: dict[str, Any], key: str) -> dict[str, Any]:
    result = value.get(key)
    if not isinstance(result, dict):
        fail(f"{key} must be an object")
    return result


def required_string(value: dict[str, Any], key: str) -> str:
    result = value.get(key)
    if not isinstance(result, str) or not result:
        fail(f"{key} must be a non-empty string")
    return result


def find_markers(value: Any, path: str = "$") -> list[str]:
    found: list[str] = []
    if isinstance(value, dict):
        for key, child in value.items():
            found.extend(find_markers(child, f"{path}.{key}"))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            found.extend(find_markers(child, f"{path}[{index}]"))
    elif isinstance(value, str):
        lowered = value.strip().lower()
        if any(marker in lowered for marker in FORBIDDEN_MARKERS):
            found.append(f"{path}={value!r}")
    return found


def validate_spec(spec: dict[str, Any], permit_fixture: bool) -> None:
    require(spec.get("schema_version") == SPEC_VERSION, f"schema_version must be {SPEC_VERSION}")
    for section in ("experiment", "printer", "process", "model", "variants", "measurement_protocol",
                    "acceptance", "abort_exclusion", "evidence", "sample_plan", "randomization"):
        required_object(spec, section)
    printer = required_object(spec, "printer")
    process = required_object(spec, "process")
    model = required_object(spec, "model")
    variants = required_object(spec, "variants")
    plan = required_object(spec, "sample_plan")
    randomization = required_object(spec, "randomization")
    status = required_string(printer, "identity_status")
    if status != "confirmed" and not permit_fixture:
        fail("printer.identity_status must be confirmed; templates and synthetic identities cannot qualify")
    for key in ("model", "serial", "machine_fingerprint_sha256"):
        required_string(printer, key)
    for key in ("material_family", "material_lot", "nozzle_id", "nozzle_diameter_mm", "layer_height_mm",
                "line_width_mm", "print_temperature_c", "bed_temperature_c", "fan_percent", "ambient_temperature_c"):
        require(key in process, f"process missing {key}")
    for key in ("model_id", "model_hash", "calibration_hash"):
        required_string(model, key)
    for key in ("baseline", "proposed"):
        required_object(variants, key)
        required_string(variants[key], "identity")
    for key in ("calibration_coupon_ids", "confirmatory_coupon_ids"):
        value = plan.get(key)
        require(isinstance(value, list) and value and all(isinstance(item, str) and item for item in value),
                f"sample_plan.{key} must contain coupon IDs")
    calibration_ids = set(plan["calibration_coupon_ids"])
    confirmatory_ids = set(plan["confirmatory_coupon_ids"])
    require(not calibration_ids & confirmatory_ids, "calibration and confirmatory coupon IDs must be disjoint")
    seed = randomization.get("seed")
    require(isinstance(seed, int) and seed >= 0, "randomization.seed must be a recorded non-negative integer")
    require(randomization.get("unit") == "coupon_pair", "randomization.unit must be coupon_pair")
    for section, value in (("measurement_protocol", spec["measurement_protocol"]),
                           ("acceptance", spec["acceptance"]), ("abort_exclusion", spec["abort_exclusion"]),
                           ("evidence", spec["evidence"])):
        require(value, f"{section} cannot be empty")
    markers = find_markers(spec)
    if markers and not permit_fixture:
        fail("physical specification contains synthetic/incomplete marker(s): " + "; ".join(markers[:5]))
    if not permit_fixture:
        require(printer.get("synthetic") is False, "synthetic printer metadata is ineligible")
        require(model.get("synthetic") is False and model.get("provenance") == "measured",
                "only measured, non-synthetic thermal models are eligible")
        require(model.get("calibrated") is True, "thermal model must be calibrated")


def validate_model(model: dict[str, Any], spec: dict[str, Any], machine: dict[str, Any], permit_fixture: bool) -> None:
    require(model.get("schema_version") in ("js-thermal-model-1", "js-thermal-model-fit-1"),
            "model must use a versioned thermal-model contract")
    if not permit_fixture:
        require(model.get("synthetic") is False and model.get("calibrated") is True and model.get("provenance") == "measured",
                "synthetic, inferred or uncalibrated model cannot produce a printable coupon bundle")
    printer = required_object(spec, "printer")
    expected = printer["machine_fingerprint_sha256"]
    require(sha256_bytes(canonical_json(machine)) == expected, "machine file does not match experiment fingerprint hash")
    model_hash = model.get("machine_fingerprint_hash", "")
    require(model_hash in (expected, "sha256:" + expected), "model is not bound to the experiment machine fingerprint")
    require(model.get("model_id") == spec["model"]["model_id"], "model_id differs from experiment specification")
    calibration_hash = model.get("calibration_hash", model.get("calibration_dataset_hash"))
    require(calibration_hash == spec["model"]["calibration_hash"], "model calibration hash differs from experiment specification")


def validate_proposal(proposal: dict[str, Any], spec: dict[str, Any], model: dict[str, Any], permit_fixture: bool) -> None:
    require(proposal.get("schema_version") == "js-thermal-schedule-1", "proposal schema version is not M2")
    require(proposal.get("analysis_only") is True, "coupon proposal must remain analysis-only")
    if not permit_fixture:
        require(proposal.get("recommendation_eligible") is True, "proposal is not recommendation-eligible")
    require(proposal.get("model_id") == model.get("model_id"), "proposal model does not match model file")
    require(proposal.get("model_hash") == spec["model"]["model_hash"], "proposal model hash does not match experiment specification")
    require(proposal.get("machine_fingerprint_hash") == spec["printer"]["machine_fingerprint_sha256"],
            "proposal machine fingerprint does not match experiment specification")
    baseline = proposal.get("baseline_order")
    proposed = proposal.get("proposed_order")
    require(isinstance(baseline, list) and isinstance(proposed, list) and baseline and proposed,
            "proposal must contain baseline_order and proposed_order")
    require(sorted(baseline) == sorted(proposed) and len(set(baseline)) == len(baseline),
            "proposal orders must be complete permutations")
    require(baseline != proposed, "proposal has no permitted order difference")


def line_bytes(program: bytes) -> list[bytes]:
    lines = program.splitlines(keepends=True)
    require(program.endswith(b"\n"), "program must end with a newline")
    return lines


def executable_count(lines: list[bytes]) -> int:
    return sum(1 for line in lines if line.decode("utf-8", errors="strict").split(";", 1)[0].strip())


def path_records(packet: dict[str, Any]) -> list[dict[str, Any]]:
    paths = packet.get("paths")
    require(isinstance(paths, list) and paths, "coupon packet must contain path records")
    result = []
    seen: set[str] = set()
    for path in paths:
        require(isinstance(path, dict), "each packet path must be an object")
        ident = required_string(path, "id")
        require(ident not in seen, f"duplicate packet path id: {ident}")
        seen.add(ident)
        for key in ("command_start", "command_end", "command_sha256", "predecessors"):
            require(key in path, f"path {ident} missing {key}")
        contract = path.get("coupon_contract")
        require(isinstance(contract, dict), f"path {ident} missing coupon_contract")
        for key in ("geometry_sha256", "extrusion_volume_mm3", "width_mm", "height_mm", "speed_mm_s",
                    "temperature_c", "fan_pwm", "tool", "material"):
            require(key in contract, f"path {ident} coupon_contract missing {key}")
        result.append(path)
    return result


def validate_ranges(program: bytes, packet: dict[str, Any]) -> tuple[list[bytes], dict[str, tuple[int, int]]]:
    lines = line_bytes(program)
    ranges: dict[str, tuple[int, int]] = {}
    occupied: list[tuple[int, int, str]] = []
    for path in path_records(packet):
        ident = path["id"]
        start = path.get("source_line_start", path["command_start"])
        end = path.get("source_line_end", path["command_end"])
        require(isinstance(start, int) and isinstance(end, int) and 1 <= start <= end <= len(lines),
                f"invalid command range for path {ident}")
        block = b"".join(lines[start - 1:end])
        require(sha256_bytes(block) == path["command_sha256"], f"path {ident} command digest mismatch")
        occupied.append((start, end, ident))
        ranges[ident] = (start, end)
    occupied.sort()
    for (_, previous_end, _), (next_start, _, _) in zip(occupied, occupied[1:]):
        require(previous_end < next_start, "path command ranges overlap")
    first, last = occupied[0][0], occupied[-1][1]
    require(all(previous_end + 1 == next_start for (_, previous_end, _), (next_start, _, _) in zip(occupied, occupied[1:])),
            "coupon path ranges must cover one contiguous emitted path interval")
    return lines, ranges


def dependency_map(paths: list[dict[str, Any]]) -> dict[str, set[str]]:
    ids = {path["id"] for path in paths}
    result: dict[str, set[str]] = {}
    for path in paths:
        predecessors = path["predecessors"]
        require(isinstance(predecessors, list) and all(isinstance(value, str) for value in predecessors),
                f"invalid predecessors for path {path['id']}")
        require(set(predecessors) <= ids, f"path {path['id']} has an unknown predecessor")
        result[path["id"]] = set(predecessors)
    return result


def check_order(order: list[str], predecessors: dict[str, set[str]]) -> None:
    position = {ident: index for index, ident in enumerate(order)}
    require(set(position) == set(predecessors) and len(position) == len(order), "order is not a complete path permutation")
    for ident, deps in predecessors.items():
        require(all(position[dep] < position[ident] for dep in deps), f"dependency violation: {ident}")


def extract_blocks(program: bytes, packet: dict[str, Any], order: list[str]) -> tuple[bytes, list[bytes], tuple[int, int]]:
    lines, ranges = validate_ranges(program, packet)
    paths = {path["id"]: path for path in path_records(packet)}
    check_order(order, dependency_map(list(paths.values())))
    first = min(start for start, _ in ranges.values())
    last = max(end for _, end in ranges.values())
    prefix = b"".join(lines[:first - 1])
    suffix = b"".join(lines[last:])
    blocks = [b"".join(lines[ranges[ident][0] - 1:ranges[ident][1]]) for ident in order]
    return prefix + suffix, blocks, (first, last)


def semantic_block(block: bytes) -> bytes:
    """Canonicalize only line endings/whitespace; command tokens remain exact."""
    rows = []
    for raw in block.splitlines():
        text = raw.decode("utf-8", errors="strict").split(";", 1)[0].strip()
        if text:
            rows.append(" ".join(text.split()))
    return ("\n".join(rows) + "\n").encode("utf-8")


def make_variant(program: bytes, packet: dict[str, Any], order: list[str],
                 manifest: dict[str, Any], proposal: dict[str, Any]) -> tuple[bytes, dict[str, Any], dict[str, Any]]:
    _, blocks, (first, last) = extract_blocks(program, packet, order)
    lines = line_bytes(program)
    prefix = b"".join(lines[:first - 1])
    suffix = b"".join(lines[last:])
    output = prefix + b"".join(blocks) + suffix
    new_packet = json.loads(json.dumps(packet))
    new_manifest = json.loads(json.dumps(manifest))
    cursor_line = first
    cursor_command = executable_count(line_bytes(prefix)) + 1
    for ident, block in zip(order, blocks):
        path = next(path for path in new_packet["paths"] if path["id"] == ident)
        block_lines = line_bytes(block)
        path["source_line_start"] = cursor_line
        path["source_line_end"] = cursor_line + len(block_lines) - 1
        path["command_start"] = cursor_command
        path["command_end"] = cursor_command + executable_count(block_lines) - 1
        path["command_sha256"] = sha256_bytes(block)
        cursor_line = path["source_line_end"] + 1
        cursor_command = path["command_end"] + 1
    new_manifest["program_sha256"] = sha256_bytes(output)
    new_packet["program_sha256"] = sha256_bytes(output)
    new_packet["manifest_sha256"] = sha256_bytes(canonical_json(new_manifest))
    new_packet["thermal_schedule_proposal"] = proposal_packet(proposal)
    return output, new_manifest, new_packet


def proposal_packet(proposal: dict[str, Any]) -> dict[str, Any]:
    ids = [str(value) for value in proposal["baseline_order"]]
    proposed = [str(value) for value in proposal["proposed_order"]]
    precedence = proposal.get("neighbor_graph", {}).get("paths", [])
    dependencies = {str(row["path_id"]): [str(value) for value in row.get("dependencies", [])] for row in precedence}
    return {"schema_version": "js-thermal-schedule-1", "model_hash": proposal["model_hash"],
            "baseline_order": ids, "proposed_order": proposed,
            "precedence": [{"path": ident, "predecessors": dependencies.get(ident, [])} for ident in proposed]}


def compare_pair(baseline: bytes, proposed: bytes, baseline_packet: dict[str, Any], proposed_packet: dict[str, Any],
                 proposal: dict[str, Any]) -> dict[str, Any]:
    bpaths = {path["id"]: path for path in path_records(baseline_packet)}
    ppaths = {path["id"]: path for path in path_records(proposed_packet)}
    require(set(bpaths) == set(ppaths), "pair path identity sets differ")
    b_lines, b_ranges = validate_ranges(baseline, baseline_packet)
    p_lines, p_ranges = validate_ranges(proposed, proposed_packet)
    differences: list[dict[str, Any]] = []
    volume = 0.0
    for ident in sorted(bpaths):
        b_block = b"".join(b_lines[b_ranges[ident][0] - 1:b_ranges[ident][1]])
        p_block = b"".join(p_lines[p_ranges[ident][0] - 1:p_ranges[ident][1]])
        b_contract = bpaths[ident]["coupon_contract"]
        p_contract = ppaths[ident]["coupon_contract"]
        require(b_contract == p_contract, f"per-path extrusion/process contract differs for {ident}")
        require(semantic_block(b_block) == semantic_block(p_block), f"path geometry/process commands differ for {ident}")
        volume += float(b_contract["extrusion_volume_mm3"])
    border = [str(value) for value in proposal["baseline_order"]]
    porder = [str(value) for value in proposal["proposed_order"]]
    actual_baseline = sorted(b_ranges, key=lambda ident: b_ranges[ident][0])
    actual_proposed = sorted(p_ranges, key=lambda ident: p_ranges[ident][0])
    require(actual_baseline == border, "baseline program order differs from recorded proposal")
    require(actual_proposed == porder, "proposed program order differs from recorded proposal")
    require(actual_baseline != actual_proposed, "pair contains no permitted order difference")
    predecessor = dependency_map(list(bpaths.values()))
    check_order(actual_baseline, predecessor)
    check_order(actual_proposed, predecessor)
    return {"schema_version": PAIR_VERSION, "path_ids": border, "baseline_order": actual_baseline,
            "proposed_order": actual_proposed, "path_count": len(border),
            "total_deposited_volume_mm3": volume,
            "permitted_differences": ["path_order", "resulting_travel", "resulting_timing"],
            "forbidden_differences": ["path_geometry", "extrusion", "width", "height", "speed", "temperature", "fan", "tool", "material"],
            "pair_integrity": "independently_recomputed"}


def run_trusted_verifier(executable: Path, program: Path, machine: Path, manifest: Path, packet: Path) -> dict[str, Any]:
    try:
        result = subprocess.run([str(executable), str(program), str(machine), str(manifest), str(packet)],
                                capture_output=True, text=True, check=False)
    except OSError as exc:
        fail(f"cannot execute independent verifier: {exc}")
    try:
        report = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        fail(f"independent verifier did not return JSON: {exc}; stderr={result.stderr.strip()}")
    require(result.returncode == 0 and report.get("accepted") is True,
            f"independent verifier rejected {program.name}: {report.get('findings', [])}")
    return report


def csv_template(ids: list[str]) -> str:
    fields = ["coupon_id", "variant", "printer_id", "material_lot", "operator", "timestamp_utc", "interface_id",
              "contact_age_s", "measured_temperature_c", "temperature_uncertainty_c", "bond_proxy_n",
              "visible_quality_score", "bridge_support_effect", "print_time_s", "excluded", "exclusion_reason", "notes"]
    rows = [fields]
    for ident in ids:
        rows.extend([[ident, "baseline", "", "", "", "", "", "", "", "", "", "", "", "", "false", "", ""],
                     [ident, "proposed", "", "", "", "", "", "", "", "", "", "", "", "", "false", "", ""]])
    return "\n".join(",".join(row) for row in rows) + "\n"


def checklist(spec: dict[str, Any]) -> str:
    printer = spec["printer"]
    return f"""# M2 coupon operator checklist

This bundle is an experiment record, not a qualification result. Do not print if any item is unchecked.

- [ ] Confirm printer model `{printer['model']}`, serial `{printer['serial']}`, firmware, nozzle and material lot match `experiment.json`.
- [ ] Confirm the machine fingerprint hash and model/calibration hashes match the bundle manifest.
- [ ] Confirm ambient, bed, nozzle temperature and fan settings before each coupon.
- [ ] Record the randomized order from `print-order.csv`; do not substitute a different order.
- [ ] Print baseline and proposed coupons with the same fixture, material lot and operator setup.
- [ ] Record contact-age thermography, destructive bond proxy, visible quality, bridge/support effects and print time.
- [ ] Mark every failed or excluded coupon with a reason; never delete a row.
- [ ] Copy raw observations and instrument calibration records into `evidence/` without editing the originals.
- [ ] Run the fit command only on registered calibration coupons; run confirmation evaluation only on confirmatory IDs.
- [ ] Do not claim M2 passed until both AD5M and AD5X records meet the preregistered rule.
"""


def prepare(args: argparse.Namespace) -> None:
    spec = read_json(args.spec)
    machine = read_json(args.machine)
    model = read_json(args.model)
    proposal = read_json(args.proposal)
    manifest = read_json(args.manifest)
    packet = read_json(args.packet)
    validate_spec(spec, args.permit_synthetic_test_fixture)
    validate_model(model, spec, machine, args.permit_synthetic_test_fixture)
    validate_proposal(proposal, spec, model, args.permit_synthetic_test_fixture)
    program = args.gcode.read_bytes()
    require(sha256_bytes(program) == packet.get("program_sha256"), "baseline program is not bound by packet")
    require(manifest.get("program_sha256") == sha256_bytes(program), "baseline program is not bound by manifest")
    paths = path_records(packet)
    ids = {str(value) for value in proposal["baseline_order"]}
    require(ids == {path["id"] for path in paths}, "proposal path IDs do not match packet paths")
    proposal_baseline = [str(value) for value in proposal["baseline_order"]]
    proposal_proposed = [str(value) for value in proposal["proposed_order"]]
    check_order(proposal_baseline, dependency_map(paths))
    check_order(proposal_proposed, dependency_map(paths))
    require(not args.out.exists(), f"refusing to overwrite immutable output directory: {args.out}")
    out_parent = args.out.parent.resolve()
    out_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="m2-coupon-", dir=out_parent) as temporary:
        root = Path(temporary)
        baseline_output, baseline_manifest, baseline_packet = make_variant(program, packet, proposal_baseline, manifest, proposal)
        proposed_output, proposed_manifest, proposed_packet = make_variant(program, packet, proposal_proposed, manifest, proposal)
        pair = compare_pair(baseline_output, proposed_output, baseline_packet, proposed_packet, proposal)
        (root / "evidence").mkdir()
        (root / "baseline").mkdir()
        (root / "proposed").mkdir()
        (root / "baseline" / "program.gcode").write_bytes(baseline_output)
        (root / "proposed" / "program.gcode").write_bytes(proposed_output)
        write_json(root / "experiment.json", spec)
        write_json(root / "machine.json", machine)
        write_json(root / "model.json", model)
        write_json(root / "proposal.json", proposal)
        write_json(root / "baseline" / "manifest.json", baseline_manifest)
        write_json(root / "baseline" / "work-packet.json", baseline_packet)
        write_json(root / "proposed" / "manifest.json", proposed_manifest)
        write_json(root / "proposed" / "work-packet.json", proposed_packet)
        write_json(root / "evidence" / "pair-integrity.json", pair)
        write_json(root / "evidence" / "input-hashes.json", {
            "experiment_sha256": sha256_bytes(canonical(spec)), "machine_sha256": sha256_bytes(canonical(machine)),
            "model_sha256": sha256_bytes(canonical(model)), "proposal_sha256": sha256_bytes(canonical(proposal)),
            "compiler": {"commit": baseline_manifest.get("compiler_commit"), "build_id": baseline_manifest.get("compiler_build_id")},
            "tool": "tools/m2_coupon.py", "tool_version": PAIR_VERSION})
        write_json(root / "evidence" / "verifier-reports.json", {"status": "pending", "required_command": "see README.md"})
        seed = spec["randomization"]["seed"]
        order = ["baseline", "proposed"]
        random.Random(seed).shuffle(order)
        (root / "print-order.csv").write_text("seed,print_index,variant\n" + "\n".join(
            f"{seed},{index},{variant}" for index, variant in enumerate(order, 1)) + "\n", encoding="utf-8", newline="\n")
        confirmatory = spec["sample_plan"]["confirmatory_coupon_ids"]
        (root / "raw-observations.csv").write_text(csv_template(confirmatory), encoding="utf-8", newline="\n")
        write_json(root / "normalized-measurements.template.json", {
            "schema_version": "js-thermal-calibration-1", "dataset_id": "REPLACE_WITH_REGISTERED_DATASET_ID",
            "printer_id": spec["printer"]["serial"], "machine_fingerprint_hash": spec["printer"]["machine_fingerprint_sha256"],
            "material_family": spec["process"]["material_family"], "material_lot": spec["process"]["material_lot"],
            "nozzle_diameter_mm": spec["process"]["nozzle_diameter_mm"], "process": {
                "layer_height_mm": spec["process"]["layer_height_mm"], "line_width_mm": spec["process"]["line_width_mm"],
                "print_temperature_c": spec["process"]["print_temperature_c"], "bed_temperature_c": spec["process"]["bed_temperature_c"],
                "fan_percent": spec["process"]["fan_percent"], "ambient_temperature_c": spec["process"]["ambient_temperature_c"]},
            "measurements": []})
        (root / "README.md").write_text("""# M2 coupon bundle\n\nThis immutable research bundle contains matched path-order variants only.\n\nTrusted verification (required before physical use):\n\n```powershell\npython tools/m2_coupon.py verify-pair --bundle . --verifier path\\to\\jslice_verify.exe\n```\n\nFit calibration data separately from confirmatory coupons:\n\n```powershell\npython tools/m2_coupon.py fit --measurements normalized-calibration.json --output model-fit.json\npython tools/m2_coupon.py evaluate --bundle . --measurements normalized-confirmation.json --output evaluation.json\n```\n\nThe bundle is not printable until both variants have accepted independent verifier reports and real AD5M/AD5X metadata is present.\n""", encoding="utf-8", newline="\n")
        (root / "operator-checklist.md").write_text(checklist(spec), encoding="utf-8", newline="\n")
        manifest = {"schema_version": PAIR_VERSION, "immutable": True, "printable": not args.permit_synthetic_test_fixture,
                    "baseline_program_sha256": sha256_bytes(baseline_output), "proposed_program_sha256": sha256_bytes(proposed_output),
                    "pair_integrity_sha256": sha256_bytes(canonical(pair))}
        write_json(root / "manifest.json", manifest)
        if args.verifier:
            reports = {}
            for variant in ("baseline", "proposed"):
                folder = root / variant
                reports[variant] = run_trusted_verifier(args.verifier, folder / "program.gcode", root / "machine.json",
                                                        folder / "manifest.json", folder / "work-packet.json")
            write_json(root / "evidence" / "verifier-reports.json", reports)
        elif not args.permit_synthetic_test_fixture:
            fail("--verifier is required for a printable bundle")
        os.replace(root, args.out)
    print(f"created immutable M2 coupon bundle: {args.out}")


def verify_pair(args: argparse.Namespace) -> None:
    root = args.bundle
    experiment = read_json(root / "experiment.json")
    proposal = read_json(root / "proposal.json")
    baseline_packet = read_json(root / "baseline" / "work-packet.json")
    proposed_packet = read_json(root / "proposed" / "work-packet.json")
    pair = compare_pair((root / "baseline" / "program.gcode").read_bytes(), (root / "proposed" / "program.gcode").read_bytes(),
                        baseline_packet, proposed_packet, proposal)
    require(pair == read_json(root / "evidence" / "pair-integrity.json"), "pair integrity report does not match independent recomputation")
    validate_spec(experiment, args.permit_synthetic_test_fixture)
    if args.verifier:
        for variant in ("baseline", "proposed"):
            run_trusted_verifier(args.verifier, root / variant / "program.gcode", root / "machine.json",
                                 root / variant / "manifest.json", root / variant / "work-packet.json")
    print(json.dumps(pair, sort_keys=True, indent=2))


def fit_measurements(args: argparse.Namespace) -> None:
    tool = Path(__file__).with_name("thermal_calibration.py")
    result = subprocess.run([sys.executable, str(tool), str(args.measurements), str(args.output)], check=False)
    if result.returncode:
        raise SystemExit(result.returncode)


def evaluate(args: argparse.Namespace) -> None:
    bundle = args.bundle
    spec = read_json(bundle / "experiment.json")
    data = read_json(args.measurements)
    plan = spec["sample_plan"]
    confirmatory = set(plan["confirmatory_coupon_ids"])
    calibration = set(plan["calibration_coupon_ids"])
    observed = {str(row.get("coupon_id")) for row in data.get("measurements", []) if isinstance(row, dict)}
    require(not observed & calibration, "confirmatory evaluation contains calibration coupon IDs")
    require(observed <= confirmatory, "measurement contains an unregistered confirmatory coupon ID")
    rows = [row for row in data.get("measurements", []) if isinstance(row, dict) and not row.get("excluded", False)]
    require(rows, "no included confirmatory measurements")
    acceptance = spec["acceptance"]
    threshold = acceptance.get("minimum_bond_proxy_improvement_percent")
    require(isinstance(threshold, (int, float)), "acceptance threshold is missing")
    by_variant: dict[str, list[float]] = {"baseline": [], "proposed": []}
    for row in rows:
        variant = row.get("variant")
        require(variant in by_variant, "measurement variant must be baseline or proposed")
        value = row.get("bond_proxy")
        require(isinstance(value, (int, float)) and value >= 0, "bond_proxy must be a non-negative number")
        by_variant[variant].append(float(value))
    require(by_variant["baseline"] and by_variant["proposed"], "both variants need included observations")
    minimum_pairs = acceptance.get("minimum_included_confirmatory_pairs", 1)
    require(len({str(row["coupon_id"]) for row in rows}) >= minimum_pairs,
            "fewer included confirmatory coupon IDs than preregistered")
    baseline = sum(by_variant["baseline"]) / len(by_variant["baseline"])
    proposed = sum(by_variant["proposed"]) / len(by_variant["proposed"])
    improvement = 100.0 * (proposed - baseline) / baseline if baseline > 0 else None
    for row in rows:
        for key in ("visible_quality_score", "print_time_s"):
            require(isinstance(row.get(key), (int, float)), f"included measurement missing {key}")
    quality = {variant: [float(row["visible_quality_score"]) for row in rows if row["variant"] == variant] for variant in by_variant}
    times = {variant: [float(row["print_time_s"]) for row in rows if row["variant"] == variant] for variant in by_variant}
    quality_delta = float(acceptance.get("visible_quality_noninferiority_delta", 0))
    time_limit = float(acceptance.get("print_time_max_increase_percent", 0))
    quality_baseline = sum(quality["baseline"]) / len(quality["baseline"])
    quality_proposed = sum(quality["proposed"]) / len(quality["proposed"])
    time_baseline = sum(times["baseline"]) / len(times["baseline"])
    time_proposed = sum(times["proposed"]) / len(times["proposed"])
    quality_pass = quality_proposed >= quality_baseline + quality_delta
    time_pass = time_proposed <= time_baseline * (1 + time_limit / 100.0)
    result = {"schema_version": "js-m2-coupon-evaluation-1", "rule": acceptance,
              "calibration_ids": sorted(calibration), "confirmatory_ids": sorted(confirmatory),
              "included_rows": len(rows), "excluded_rows": len(data.get("measurements", [])) - len(rows),
              "baseline_bond_proxy_mean": baseline, "proposed_bond_proxy_mean": proposed,
              "improvement_percent": improvement, "bond_rule_pass": improvement is not None and improvement >= float(threshold),
              "baseline_visible_quality_mean": quality_baseline, "proposed_visible_quality_mean": quality_proposed,
              "quality_rule_pass": quality_pass, "baseline_print_time_mean_s": time_baseline,
              "proposed_print_time_mean_s": time_proposed, "print_time_rule_pass": time_pass,
              "status": "pass" if improvement is not None and improvement >= float(threshold) and quality_pass and time_pass else "fail"}
    write_json(args.output, result)
    print(json.dumps(result, sort_keys=True, indent=2))


def templates(args: argparse.Namespace) -> None:
    require(not args.out.exists(), f"refusing to overwrite template directory: {args.out}")
    args.out.mkdir(parents=True)
    write_json(args.out / "experiment.template.json", {
        "schema_version": SPEC_VERSION, "experiment": {"id": "REPLACE", "registration_status": "preregistered"},
        "printer": {"model": "AD5M or AD5X", "serial": "REPLACE", "identity_status": "REPLACE_WITH_CONFIRMED",
                     "machine_fingerprint_sha256": "REPLACE_WITH_REAL_FINGERPRINT_HASH", "synthetic": False},
        "process": {"material_family": "REPLACE", "material_lot": "REPLACE", "nozzle_id": "REPLACE",
                     "nozzle_diameter_mm": 0.4, "layer_height_mm": 0.2, "line_width_mm": 0.45,
                     "print_temperature_c": 215, "bed_temperature_c": 60, "fan_percent": 0, "ambient_temperature_c": 25},
        "model": {"model_id": "REPLACE", "model_hash": "REPLACE", "calibration_hash": "REPLACE"},
        "variants": {"baseline": {"identity": "source-order"}, "proposed": {"identity": "m2-proposal"}},
        "measurement_protocol": {"required": ["contact_age_thermography", "destructive_bond_proxy", "visible_quality", "print_time"]},
        "acceptance": {"minimum_bond_proxy_improvement_percent": 10, "visible_quality_noninferiority_delta": 0,
                       "print_time_max_increase_percent": 5},
        "abort_exclusion": {"criteria": ["thermal runaway", "failed adhesion", "unrecorded intervention"]},
        "evidence": {"files": ["raw-observations.csv", "instrument-calibration", "photos", "verifier-reports"]},
        "sample_plan": {"calibration_coupon_ids": ["CAL-1", "CAL-2", "CAL-3"], "confirmatory_coupon_ids": ["CONF-1", "CONF-2"]},
        "randomization": {"unit": "coupon_pair", "seed": 0}})
    (args.out / "README.txt").write_text("Supply real AD5M/AD5X printer serial, firmware/startup assumptions, fingerprint, material lot, measured model/calibration hash, and a preregistered proposal before prepare can create printable output.\n", encoding="utf-8")
    print(f"created fail-closed M2 input templates: {args.out}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    init = sub.add_parser("templates")
    init.add_argument("--out", type=Path, required=True)
    prep = sub.add_parser("prepare")
    prep.add_argument("--spec", type=Path, required=True)
    prep.add_argument("--gcode", type=Path, required=True)
    prep.add_argument("--machine", type=Path, required=True)
    prep.add_argument("--manifest", type=Path, required=True)
    prep.add_argument("--packet", type=Path, required=True)
    prep.add_argument("--proposal", type=Path, required=True)
    prep.add_argument("--model", type=Path, required=True)
    prep.add_argument("--out", type=Path, required=True)
    prep.add_argument("--verifier", type=Path)
    prep.add_argument("--permit-synthetic-test-fixture", action="store_true")
    verify = sub.add_parser("verify-pair")
    verify.add_argument("--bundle", type=Path, required=True)
    verify.add_argument("--verifier", type=Path)
    verify.add_argument("--permit-synthetic-test-fixture", action="store_true")
    fit_parser = sub.add_parser("fit")
    fit_parser.add_argument("--measurements", type=Path, required=True)
    fit_parser.add_argument("--output", type=Path, required=True)
    eval_parser = sub.add_parser("evaluate")
    eval_parser.add_argument("--bundle", type=Path, required=True)
    eval_parser.add_argument("--measurements", type=Path, required=True)
    eval_parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "templates": templates(args)
        elif args.command == "prepare": prepare(args)
        elif args.command == "verify-pair": verify_pair(args)
        elif args.command == "fit": fit_measurements(args)
        elif args.command == "evaluate": evaluate(args)
        return 0
    except CouponError as exc:
        print(f"m2 coupon rejected: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
