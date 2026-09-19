#!/usr/bin/env python3
"""Fit the bounded exponential cooling contract from measured coupon data.

This tool only produces a calibration artifact when the input is complete and
internally consistent.  It never invents physical measurements and it marks
the result measured only when the supplied dataset passes all checks.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Any


SCHEMA_VERSION = "js-thermal-calibration-1"
FIT_SCHEMA_VERSION = "js-thermal-model-fit-1"


def fail(message: str) -> None:
    raise ValueError(message)


def read_dataset(path: Path) -> tuple[dict[str, Any], str]:
    raw = path.read_bytes()
    try:
        data = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        fail(f"invalid JSON: {exc}")
    if not isinstance(data, dict) or data.get("schema_version") != SCHEMA_VERSION:
        fail("schema_version must be js-thermal-calibration-1")
    required = ("dataset_id", "printer_id", "machine_fingerprint_hash",
                "material_family", "nozzle_diameter_mm", "process", "measurements")
    for key in required:
        if key not in data:
            fail(f"missing required field: {key}")
    measurements = data["measurements"]
    if not isinstance(measurements, list) or len(measurements) < 3:
        fail("at least three measurements are required")
    process = data["process"]
    if not isinstance(process, dict):
        fail("process must be an object")
    for key in ("layer_height_mm", "line_width_mm", "print_temperature_c",
                "bed_temperature_c", "fan_percent", "ambient_temperature_c"):
        if key not in process:
            fail(f"missing process field: {key}")
    for row in measurements:
        if not isinstance(row, dict):
            fail("each measurement must be an object")
        for key in ("coupon_id", "contact_age_s", "measured_temperature_c", "bond_proxy"):
            if key not in row:
                fail(f"missing measurement field: {key}")
        values = (row["contact_age_s"], row["measured_temperature_c"], row["bond_proxy"])
        if any(not isinstance(value, (int, float)) or not math.isfinite(float(value)) for value in values):
            fail("measurement values must be finite numbers")
        if float(row["contact_age_s"]) < 0 or float(row["bond_proxy"]) < 0:
            fail("contact_age_s and bond_proxy must be non-negative")
    if float(data["nozzle_diameter_mm"]) <= 0:
        fail("nozzle_diameter_mm must be positive")
    return data, hashlib.sha256(raw).hexdigest()


def fit(data: dict[str, Any], dataset_hash: str) -> dict[str, Any]:
    process = data["process"]
    ambient = float(process["ambient_temperature_c"])
    deposition = float(process["print_temperature_c"])
    if deposition <= ambient:
        fail("print temperature must exceed ambient temperature")
    xs: list[float] = []
    ys: list[float] = []
    for row in data["measurements"]:
        age = float(row["contact_age_s"])
        ratio = (float(row["measured_temperature_c"]) - ambient) / (deposition - ambient)
        if ratio <= 0 or ratio > 1.0000001:
            fail("measured temperature is outside the exponential fit domain")
        xs.append(age)
        ys.append(math.log(max(ratio, 1e-12)))
    mean_x = sum(xs) / len(xs)
    mean_y = sum(ys) / len(ys)
    denominator = sum((x - mean_x) ** 2 for x in xs)
    if denominator <= 1e-12:
        fail("contact ages must span a non-zero interval")
    slope = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys)) / denominator
    if slope >= 0:
        fail("measurements do not show cooling")
    intercept = mean_y - slope * mean_x
    tau = -1.0 / slope
    residuals = [y - (intercept + slope * x) for x, y in zip(xs, ys)]
    rms = math.sqrt(sum(residual * residual for residual in residuals) / len(residuals))
    return {
        "schema_version": FIT_SCHEMA_VERSION,
        "model_id": f"thermal-fit-{data['dataset_id']}",
        "model_version": "1",
        "machine_fingerprint_hash": data["machine_fingerprint_hash"],
        "printer_id": data["printer_id"],
        "material_family": data["material_family"],
        "material_lot": data.get("material_lot", ""),
        "nozzle_diameter_mm": float(data["nozzle_diameter_mm"]),
        "layer_height_mm": float(process["layer_height_mm"]),
        "line_width_mm": float(process["line_width_mm"]),
        "ambient_temperature_c": ambient,
        "deposition_temperature_c": deposition,
        "bed_temperature_c": float(process["bed_temperature_c"]),
        "fan_percent": float(process["fan_percent"]),
        "cooling_time_constant_s": tau,
        "fit_log_residual_rms": rms,
        "fit_sample_count": len(xs),
        "calibration_dataset_hash": f"sha256:{dataset_hash}",
        "provenance": "measured",
        "calibrated": True,
        "synthetic": False,
        "diagnostics": {
            "contact_age_min_s": min(xs),
            "contact_age_max_s": max(xs),
            "temperature_min_c": min(float(row["measured_temperature_c"]) for row in data["measurements"]),
            "temperature_max_c": max(float(row["measured_temperature_c"]) for row in data["measurements"]),
            "uncertainty_c": max(1.0, rms * (deposition - ambient)),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data, dataset_hash = read_dataset(args.input)
    result = fit(data, dataset_hash)
    args.output.write_text(json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValueError as exc:
        raise SystemExit(f"thermal calibration rejected: {exc}")
