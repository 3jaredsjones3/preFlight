#!/usr/bin/env python3
"""Create and validate fail-closed AD5M/AD5X machine-evidence bootstrap records.

This tool records published facts and physical observations without pretending
that a printer has been measured or qualified.  It is deliberately separate
from ordinary slicing and from the M2 thermal scheduler.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import sys
from pathlib import Path
from typing import Any


SCHEMA_VERSION = "js-machine-evidence-1"
TODAY = "2026-09-19"
SOURCE_CLASSES = {"manufacturer", "slicer_profile", "community_firmware", "measured", "user_supplied"}
QUALIFICATION_STATES = {"published", "corroborative", "observed", "unqualified", "ambiguous", "missing"}


class EvidenceError(ValueError):
    pass


def fail(message: str) -> None:
    raise EvidenceError(message)


def canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail(f"cannot read JSON {path}: {exc}")


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(canonical(value))


def source(source_class: str, artifact: str, revision: str | None = None,
           observed_on: str = TODAY, qualification_state: str = "published",
           uncertainty: Any = None) -> dict[str, Any]:
    if source_class not in SOURCE_CLASSES:
        fail(f"unsupported source class: {source_class}")
    if qualification_state not in QUALIFICATION_STATES:
        fail(f"unsupported qualification state: {qualification_state}")
    return {"class": source_class, "artifact": artifact, "revision": revision,
            "observation_date": observed_on, "qualification_state": qualification_state,
            "uncertainty": uncertainty}


def datum(identifier: str, value: Any, units: str, provenance: dict[str, Any],
          qualification_state: str | None = None, uncertainty: Any = None) -> dict[str, Any]:
    result = {"id": identifier, "value": value, "units": units, "source": provenance,
              "qualification_state": qualification_state or provenance["qualification_state"],
              "uncertainty": uncertainty}
    return result


def manufacturer(url: str) -> dict[str, Any]:
    return source("manufacturer", url)


def profile(path: str) -> dict[str, Any]:
    return source("slicer_profile", f"https://github.com/OrcaSlicer/OrcaSlicer/blob/c168d0c8db3804ebc13879fb96cbb80df967410a/{path}",
                  "c168d0c8db3804ebc13879fb96cbb80df967410a")


def community(url: str, revision: str) -> dict[str, Any]:
    return source("community_firmware", url, revision, qualification_state="corroborative")


AD5M_URL = "https://www.flashforge.com/products/adventurer-5m-3d-printer"
AD5X_URL = "https://www.flashforge.com/products/flashforge-ad5x-3d-printer"
AD5M_PROFILE = "resources/profiles/Flashforge/machine/fdm_adventurer5m_common.json"
AD5X_PROFILE = "resources/profiles/Flashforge/machine/Flashforge%20AD5X%200.4%20nozzle.json"


def common_sources(model: str) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    if model == "AD5M":
        return manufacturer(AD5M_URL), profile(AD5M_PROFILE), community(
            "https://github.com/xblax/flashforge_ad5m_klipper_mod/blob/2500307003bfb5c81b23894de8be42c2c4d30766/printer_configs/printer.base.cfg",
            "2500307003bfb5c81b23894de8be42c2c4d30766")
    if model == "AD5X":
        return manufacturer(AD5X_URL), profile(AD5X_PROFILE), community(
            "https://github.com/ghzserg/zmod/blob/bf302618295f7d21326c7b7b256632bf8da55302/Native_firmware/config/ad5x/printer.base.cfg",
            "bf302618295f7d21326c7b7b256632bf8da55302")
    fail("model must be AD5M or AD5X")


def facts_for(model: str) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    mf, sf, cf = common_sources(model)
    manufacturer_facts = [
        datum("build_volume", [220, 220, 220], "mm", mf),
        datum("maximum_nozzle_temperature", 280 if model == "AD5M" else 300, "degC", mf),
        datum("maximum_bed_temperature", 100 if model == "AD5M" else 110, "degC", mf),
        datum("advertised_maximum_speed", 600 if model == "AD5M" else 300, "mm/s", mf),
        datum("maximum_acceleration", 20000, "mm/s^2", mf),
        datum("nozzle_options", ["0.25", "0.4", "0.6", "0.8"], "mm", mf),
    ]
    if model == "AD5M":
        manufacturer_facts.append(datum("external_body", {"excluding_display_spool_holder": [363, 376, 413],
                                                              "including_display_excluding_spool_holder": [363, 402, 448]}, "mm", mf))
    else:
        manufacturer_facts.extend([
            datum("maximum_travel_speed", 600, "mm/s", mf),
            datum("default_nozzle", 0.4, "mm", mf),
            datum("architecture", "single-nozzle single-extruder multi-material", "description", mf),
            datum("nozzle_design_note", "Flashforge says the nozzle design was adjusted from the 5M series", "description", mf),
        ])
    profile_facts = [
        datum("printable_xy", [-110, 110, -110, 110] if model == "AD5M" else [0, 220, 0, 220], "mm [xmin,xmax,ymin,ymax]", sf),
        datum("printable_z", 220, "mm", sf),
        datum("profile_xy_speed", 600, "mm/s", sf),
        datum("profile_z_speed", 20, "mm/s", sf),
        datum("profile_e_speed", 30, "mm/s", sf),
        datum("xy_travel_extrusion_acceleration", 20000, "mm/s^2", sf),
        datum("z_acceleration", 500, "mm/s^2", sf),
        datum("e_retraction_acceleration", 5000, "mm/s^2", sf),
        datum("extruder_clearance_radius", 76 if model == "AD5M" else 58, "mm", sf, uncertainty="heuristic profile clearance"),
        datum("clearance_height_to_rod", 27 if model == "AD5M" else 26, "mm", sf),
        datum("clearance_height_to_lid", 150 if model == "AD5M" else 130, "mm", sf),
        datum("profile_start_program", None, "verbatim_gcode_required_capture", sf, "missing"),
        datum("profile_start_program_sha256", None, "sha256_required_after_capture", sf, "missing"),
        datum("hotend_descriptor", "flashforge_adventurer_5m_series_hotend.stl", "profile_asset_identity", sf,
              "unqualified", "9.955 x 9.955 x 32.505 mm approximate bounding box; visualization only"),
    ]
    if model == "AD5M":
        profile_facts.append(datum("extrusion_mode", "relative", "mode", sf))
    else:
        profile_facts.extend([
            datum("nozzle_height", 4, "mm", sf),
            datum("retraction", 2, "mm", sf),
            datum("tool_change_retraction", 5, "mm", sf),
            datum("cut_retraction", 18, "mm", sf),
            datum("single_extruder_multi_material", True, "boolean", sf),
        ])
    community_facts = [
        datum("community_axis_travel", [-110, 110.1, -110, 110.1, -10, 230] if model == "AD5M" else [-20, 225, -20, 232, -10, 230],
              "mm [xmin,xmax,ymin,ymax,zmin,zmax]", cf),
        datum("community_bed_mesh", [-105, 105, -105, 105] if model == "AD5M" else [0, 215, 0, 215],
              "mm [xmin,xmax,ymin,ymax]", cf),
        datum("community_kinematics", "CoreXY", "description", cf),
        datum("community_speed", 600, "mm/s", cf),
        datum("community_acceleration", 20000, "mm/s^2", cf),
    ]
    materials = [
        datum("generic_pla_seed", {"nozzle_temperature": 220, "profile_range": [190, 230], "bed_first_layer": 55,
                                    "bed_subsequent": 50, "flow_ratio": 0.98, "max_volumetric_speed": 25},
              "degC/mm^3/s/mixed", sf, "unqualified", "uncalibrated Orca slicer default"),
        datum("generic_petg_seed", {"nozzle_temperature": 255, "profile_range": [220, 260], "bed": 70,
                                     "flow_ratio": 1.0, "max_volumetric_speed": 12, "cooling_after_first_layer": [80, 100]},
              "degC/mm^3/s/percent", sf, "unqualified", "uncalibrated Orca slicer default"),
    ]
    facts = manufacturer_facts + profile_facts + community_facts + materials
    conflicts = [
        {"subject": "coordinate_convention", "values": [
            {"value": [-110, 110, -110, 110], "units": "mm [xmin,xmax,ymin,ymax]", "source": sf},
            {"value": [0, 220, 0, 220], "units": "mm [xmin,xmax,ymin,ymax]", "source": sf},
        ], "interpretation": "AD5M centered coordinates versus AD5X corner-origin coordinates; never normalize silently"},
        {"subject": "published_vs_qualified_limits", "values": [
            {"value": "manufacturer advertised/profile limits", "source": mf},
            {"value": None, "units": "qualified coupon operating envelope", "source": source("measured", "physical-coupon-record-required", qualification_state="missing")},
        ], "interpretation": "advertised and profile limits are not calibrated coupon limits"},
        {"subject": "thermal_limit_vs_firmware_ceiling", "values": [
            {"value": "manufacturer thermal rating", "source": mf},
            {"value": None, "units": "firmware control ceiling", "source": source("community_firmware", cf["artifact"], cf["revision"], qualification_state="corroborative")},
        ], "interpretation": "firmware max_temp is not a physical rating"},
        {"subject": "startup_behavior", "values": [
            {"value": "Orca profile start program", "source": sf},
            {"value": None, "units": "actual Flash Studio/firmware startup sequence", "source": source("measured", "physical-export-observation-required", qualification_state="missing")},
        ], "interpretation": "profile G-code must not be treated as actual exported startup behavior"},
    ]
    if model == "AD5X":
        conflicts.append({"subject": "hotend_geometry", "values": [
            {"value": "5M-series STL descriptor; visualization prior only", "source": sf},
            {"value": "AD5X nozzle design differs; exact geometry not inherited", "source": mf},
        ], "interpretation": "AD5X requires printer/nozzle-specific measured geometry"})
    return facts + materials, conflicts


REQUIRED_FIELDS = {
    "serial": "actual printer serial",
    "firmware_identity": "stock/modded firmware identity",
    "firmware_version": "exact firmware version",
    "nozzle_identifier": "nozzle diameter/type or identifier",
    "nozzle_diameter_mm": "nozzle diameter",
    "slicer_profile_identity": "slicer/profile identity",
    "slicer_profile_hash": "slicer/profile hash",
    "exported_start_gcode": "exported start G-code observation",
    "exported_end_gcode": "exported end G-code observation",
    "exported_tool_change_gcode": "exported tool-change G-code observation",
    "material_manufacturer": "material manufacturer",
    "material_type": "material type",
    "material_color": "material color",
    "material_lot": "material lot",
    "ambient_temperature_c": "ambient-condition observation",
    "operator": "operator identity",
    "observation_date": "observation date",
    "coordinate_convention": "actual G-code coordinate convention",
}


def empty_observations() -> dict[str, Any]:
    return {}


def observation_source(observations: dict[str, Any]) -> dict[str, Any]:
    cls = observations.get("source_class", "user_supplied")
    artifact = observations.get("artifact_identity", "user-observation-record-required")
    return source(cls, artifact, observations.get("revision"), observations.get("observation_date", TODAY), "observed")


def make_record(field: str, value: Any, observations: dict[str, Any]) -> dict[str, Any]:
    units = "mm" if field == "nozzle_diameter_mm" else "degC" if field == "ambient_temperature_c" else "verbatim_gcode" if field.startswith("exported_") else "identifier"
    return datum(field, value, units, observation_source(observations), "observed")


def merge_observations(artifact: dict[str, Any], observations: dict[str, Any]) -> None:
    if not isinstance(observations, dict):
        fail("observation input must be a JSON object")
    fields = observations.get("fields", observations)
    if not isinstance(fields, dict):
        fail("observations.fields must be an object")
    binding = artifact["machine_binding"]
    for field in REQUIRED_FIELDS:
        raw = fields.get(field)
        if isinstance(raw, dict) and "value" in raw:
            value = raw["value"]
        else:
            value = raw
        if value is not None:
            binding[field] = make_record(field, value, observations)
            if field == "serial":
                artifact["machine"]["serial"] = value
                artifact["machine"]["identity_state"] = "observed"
    for raw_name, hash_name in (("exported_start_gcode", "exported_start_gcode_sha256"),
                                ("exported_end_gcode", "exported_end_gcode_sha256"),
                                ("exported_tool_change_gcode", "exported_tool_change_gcode_sha256")):
        value = binding.get(raw_name, {}).get("value")
        if isinstance(value, str) and value:
            binding[hash_name] = make_record(hash_name, sha256(value.encode("utf-8")), observations)
    profile_text = fields.get("slicer_profile_text")
    profile_hash = fields.get("slicer_profile_hash")
    if isinstance(profile_text, str) and profile_hash is not None:
        actual = sha256(profile_text.encode("utf-8"))
        supplied = profile_hash.get("value") if isinstance(profile_hash, dict) else profile_hash
        if supplied != actual:
            fail("slicer profile hash does not match supplied profile text")


def record_value(record: Any) -> Any:
    return record.get("value") if isinstance(record, dict) else None


def qualification_report(artifact: dict[str, Any]) -> dict[str, Any]:
    binding = artifact.get("machine_binding", {})
    blockers: list[str] = []
    for field, description in REQUIRED_FIELDS.items():
        value = record_value(binding.get(field))
        if value is None or value == "":
            blockers.append(f"{field}: missing {description}")
    convention = record_value(binding.get("coordinate_convention"))
    if isinstance(convention, dict) and (convention.get("ambiguous") is True or convention.get("name") in (None, "", "ambiguous", "unknown")):
        blockers.append("coordinate_convention: ambiguous actual G-code convention")
    elif isinstance(convention, str) and convention.strip().lower() in {"", "ambiguous", "unknown", "tbd"}:
        blockers.append("coordinate_convention: ambiguous actual G-code convention")
    if record_value(binding.get("firmware_version")) in ("unknown", "ambiguous", "tbd"):
        blockers.append("firmware_version: exact firmware identity is ambiguous")
    startup = [record_value(binding.get(name)) for name in ("exported_start_gcode", "exported_end_gcode", "exported_tool_change_gcode")]
    if any(value in ("unknown", "ambiguous", "tbd") for value in startup):
        blockers.append("startup_behavior: exported startup behavior is ambiguous")
    return {"qualification_state": "unqualified", "coupon_bundle_eligible_for_review": not blockers,
            "physical_qualification_claim": False, "blockers": sorted(set(blockers))}


def source_integrity(artifact: dict[str, Any]) -> str:
    payload = {"published_facts": artifact["published_facts"], "conflicts": artifact["conflicts"]}
    return sha256(canonical_json(payload))


def build_artifact(model: str, observations: dict[str, Any] | None = None,
                   source_records: dict[str, Any] | None = None) -> dict[str, Any]:
    facts, conflicts = facts_for(model)
    artifact = {
        "schema_version": SCHEMA_VERSION,
        "artifact_kind": "machine-evidence-bootstrap",
        "machine": {"model": model, "serial": None, "identity_state": "draft"},
        "published_facts": facts,
        "conflicts": conflicts,
        "machine_binding": empty_observations(),
        "collision_geometry": None,
        "geometry_policy": {"image_generated_or_inferred_is_unqualified": True,
                            "exact_collision_proof_requires_measured_qualified_mesh": True},
        "required_observations": list(REQUIRED_FIELDS.values()) + [
            "profile start program captured verbatim and hashed",
            "profile/end/tool-change export evidence retained as immutable artifacts",
            "physical measurement worksheet completed for this printer",
        ],
        "qualification": {"qualification_state": "unqualified", "coupon_bundle_eligible_for_review": False,
                           "physical_qualification_claim": False, "blockers": []},
    }
    if source_records is not None:
        if not isinstance(source_records, dict):
            fail("source records must be a JSON object")
        extra_facts = source_records.get("published_facts", [])
        if not isinstance(extra_facts, list):
            fail("source records published_facts must be an array")
        artifact["published_facts"].extend(copy.deepcopy(extra_facts))
        artifact["conflicts"].extend(copy.deepcopy(source_records.get("conflicts", [])))
    if observations is not None:
        merge_observations(artifact, observations)
    artifact["qualification"] = qualification_report(artifact)
    artifact["source_integrity_sha256"] = source_integrity(artifact)
    return artifact


def validate_collision_geometry(geometry: Any, exact: bool = False) -> None:
    if geometry is None:
        return
    if not isinstance(geometry, dict):
        fail("collision_geometry must be an object or null")
    required = ("coordinate_frame", "units", "nozzle_tip_origin", "mesh_hash", "transform",
                "fixed_components", "moving_components", "applicable_printer_nozzle_state",
                "uncertainty_inflation_margin", "provenance", "qualification_state")
    for key in required:
        if key not in geometry:
            fail(f"collision_geometry missing {key}")
    if geometry["qualification_state"] != "qualified":
        geometry["exact_proof_eligible"] = False
        if exact:
            fail("inferred or unqualified collision geometry cannot satisfy exact collision proof")
    provenance = geometry["provenance"]
    if isinstance(provenance, dict) and provenance.get("class") in {"user_supplied", "community_firmware"}:
        if exact:
            fail("collision geometry provenance is not a measured qualified mesh")


def validate_artifact(artifact: dict[str, Any], require_review_ready: bool = False,
                      exact_collision: bool = False) -> None:
    if artifact.get("schema_version") != SCHEMA_VERSION:
        fail(f"schema_version must be {SCHEMA_VERSION}")
    if artifact.get("artifact_kind") != "machine-evidence-bootstrap":
        fail("artifact_kind is not machine-evidence-bootstrap")
    model = artifact.get("machine", {}).get("model")
    if model not in {"AD5M", "AD5X"}:
        fail("machine.model must be AD5M or AD5X")
    expected = source_integrity(artifact)
    if artifact.get("source_integrity_sha256") != expected:
        fail("source/profile fact integrity hash mismatch")
    validate_collision_geometry(artifact.get("collision_geometry"), exact_collision)
    report = qualification_report(artifact)
    if artifact.get("qualification") != report:
        fail("qualification report is stale or was modified")
    if require_review_ready and report["blockers"]:
        fail("machine evidence is not eligible for physical coupon review: " + "; ".join(report["blockers"]))


def worksheet(model: str) -> str:
    extra = "- AD5X cutter, wiper/purge, filament-handling obstacles\n" if model == "AD5X" else "- Any printer-specific fixed/moving obstacles\n"
    return f"""# {model} machine measurement worksheet

Status: unqualified until every required field is captured and reviewed. Do not invent values.
Printer serial: ____________________    Firmware identity/version: ____________________
Nozzle identifier/type/diameter: ____________________    Slicer/profile/hash: ____________________
Operator: ____________________    Observation date/time/timezone: ____________________
Material manufacturer/type/color/lot: _________________________________________________
Ambient temperature/humidity: _______________________________________________________
Actual G-code coordinate convention (including origin, units, absolute/relative modes):
____________________________________________________________________________________

## Nozzle-axis measurements (mm)

| Field | Measurement | Instrument/uncertainty | Photo/fiducial reference |
|---|---:|---|---|
| Toolhead left extent from nozzle axis | | | |
| Toolhead right extent from nozzle axis | | | |
| Toolhead front extent from nozzle axis | | | |
| Toolhead back extent from nozzle axis | | | |
| Nozzle tip to lowest non-nozzle component | | | |
| Carriage height above nozzle | | | |
| Cable/PTFE swept envelope at bed corners | | | |
| Rod/gantry clearance | | | |
| Lid clearance | | | |
| Bed physical left/right/front/back edges | | | |
| Bed tabs/clips/raised features | | | |

{extra}
## Evidence capture

- Exported start G-code artifact path/hash: __________________________________________
- Exported end G-code artifact path/hash: ____________________________________________
- Exported tool-change G-code artifact path/hash: ____________________________________
- Profile start program captured verbatim/hash: _______________________________________
- Photos: path, camera, lens, date, scale/fiducial and orientation for each image:
  ____________________________________________________________________________________
  ____________________________________________________________________________________
- Fixed components measured separately from moving components: ________________________
- Coordinate frame, units, nozzle-tip origin and transform: ___________________________
- Mesh hash and inflation/uncertainty margin, if any: _________________________________

## Fields still preventing qualification

____________________________________________________________________________________
____________________________________________________________________________________
____________________________________________________________________________________
"""


def command_bootstrap(args: argparse.Namespace) -> None:
    observations = read_json(args.observations) if args.observations else None
    sources = read_json(args.source_records) if args.source_records else None
    artifact = build_artifact(args.model, observations, sources)
    if args.require_review_ready:
        validate_artifact(artifact, require_review_ready=True)
    write_json(args.output, artifact)
    print(json.dumps({"output": str(args.output), "model": args.model,
                      "qualification": artifact["qualification"]}, sort_keys=True, indent=2))


def command_validate(args: argparse.Namespace) -> None:
    artifact = read_json(args.input)
    validate_artifact(artifact, args.require_review_ready, args.exact_collision_proof)
    print(json.dumps({"valid": True, "qualification": artifact["qualification"]}, sort_keys=True, indent=2))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    bootstrap = sub.add_parser("bootstrap")
    bootstrap.add_argument("--model", choices=["AD5M", "AD5X"], required=True)
    bootstrap.add_argument("--observations", type=Path)
    bootstrap.add_argument("--source-records", type=Path)
    bootstrap.add_argument("--output", type=Path, required=True)
    bootstrap.add_argument("--require-review-ready", action="store_true")
    valid = sub.add_parser("validate")
    valid.add_argument("--input", type=Path, required=True)
    valid.add_argument("--require-review-ready", action="store_true")
    valid.add_argument("--exact-collision-proof", action="store_true")
    sheet = sub.add_parser("worksheet")
    sheet.add_argument("--model", choices=["AD5M", "AD5X"], required=True)
    sheet.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "bootstrap":
            command_bootstrap(args)
        elif args.command == "validate":
            command_validate(args)
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            if args.output.exists():
                fail(f"refusing to overwrite worksheet: {args.output}")
            args.output.write_text(worksheet(args.model), encoding="utf-8", newline="\n")
            print(f"created {args.model} measurement worksheet: {args.output}")
        return 0
    except (EvidenceError, OSError) as exc:
        print(f"machine evidence rejected: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
