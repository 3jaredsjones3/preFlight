# M2.1 machine-evidence bootstrap

M2.1 is analysis/evidence infrastructure for the physical AD5M and AD5X
coupon program. It does not reorder paths, alter ordinary slicing, lower a
thermal schedule, or qualify either printer.

## Tooling

`tools/machine_evidence.py` creates deterministic draft records from the pinned
manufacturer, OrcaSlicer, and community-firmware source facts:

```powershell
python tools/machine_evidence.py bootstrap --model AD5M --output build/m2.1/AD5M.bootstrap.json
python tools/machine_evidence.py bootstrap --model AD5X --output build/m2.1/AD5X.bootstrap.json
python tools/machine_evidence.py observations-template --model AD5M --output build/m2.1/AD5M.observations.json
python tools/machine_evidence.py observations-template --model AD5X --output build/m2.1/AD5X.observations.json
python tools/machine_evidence.py worksheet --model AD5M --output build/m2.1/AD5M.measurements.md
python tools/machine_evidence.py worksheet --model AD5X --output build/m2.1/AD5X.measurements.md
```

The draft records retain value, units, source class, exact URL or artifact
identity, pinned revision, observation date, qualification state, uncertainty,
and explicit conflicts. The AD5M centered coordinate convention is not merged
with the AD5X corner-origin convention. Manufacturer limits, profile limits,
community firmware ceilings, and future coupon limits remain separate. The
5M-series hotend STL identity is retained only as an unqualified visualization
prior; AD5X does not inherit it as exact geometry.

The observations templates preserve null placeholders for physical values. All
captured SHA-256 values must be 64 lowercase hexadecimal characters. This
includes the slicer profile, machine artifact, start/end sequence artifacts,
AD5X tool-change artifact, and any collision mesh.

## Startup evidence applicability

Every exported sequence uses the same applicability record. `required` needs a
real artifact identity and SHA-256. `not_applicable` needs a machine-capability
reason and cannot contain an artifact or hash. AD5M tool change is explicitly
`not_applicable` because the single-material machine has no ordinary tool-change
sequence; Jared must not create or hash an empty/N/A file. AD5X tool change is
`required` because its multi-material workflow has a real sequence to capture.

The two Python validators and two JSON schemas express the same capability
decision at different boundaries: `tools/machine_evidence.py` admits draft
observations, while `tools/m2_coupon.py` admits coupon specifications.
`schema/machine-evidence.schema.json` and
`schema/m2-coupon-experiment.schema.json` describe those artifacts. A capability
change must update all four and exercise `tools/machine_evidence_tests.py` and
`tools/m2_coupon_tests.py`; otherwise a draft can pass one boundary and fail
another. Draft null placeholders remain distinct from complete coupon evidence.

## Observation ingestion

Supply observations in the generated JSON record with `fields` for the actual
serial, firmware identity/version, nozzle, slicer/profile and hash, machine
artifact identity/hash, applicable sequence evidence, material manufacturer/
type/color/lot, ambient conditions, operator/date, and actual coordinate
convention:

```powershell
python tools/machine_evidence.py bootstrap --model AD5X `
  --observations path/to/AD5X.observations.json `
  --output build/m2.1/AD5X.observed.json `
  --require-review-ready
python tools/machine_evidence.py validate --input build/m2.1/AD5X.observed.json --require-review-ready
```

`--require-review-ready` is intended to require complete identity/export evidence
for physical review; it never changes `qualification_state: unqualified` and
never sets a physical qualification claim. Unknown or ambiguous firmware,
startup behavior, or coordinates must fail closed. The open findings below mean
the current implementation does not fully enforce that requirement. A supplied
profile text/hash mismatch and malformed, uppercase, or shortened SHA-256 values
are rejected, but hash syntax alone does not verify capture or authenticate it.

The optional `collision_geometry` object records coordinate frame, units,
nozzle-tip origin, mesh hash, transform, fixed and moving components,
printer/nozzle state, inflation margin, provenance, and qualification state.
Image-generated, inferred, community, or otherwise unqualified meshes cannot
satisfy exact collision-proof requirements.

## Open validation findings

Self-review on 2026-09-22 reproduced these gaps against `de88ce8`. The probes
used only the existing synthetic `MachineEvidenceTests.complete_observations`
helper, `build_artifact`, and `validate_artifact(..., require_review_ready=True)`
in memory. They created no physical observations or printable packages.

| Input to review-readiness validation | Observed result | Required follow-up |
| --- | --- | --- |
| Complete test binding with no verbatim profile-start program or its digest | Accepted; both `profile_start_program` facts remain null | Capture and bind the actual profile-start evidence; reject absent text/digest and mismatches before review readiness |
| Same binding with `coordinate_convention: false` | Accepted | Validate the coordinate value's type and required convention information, then reject ambiguous or incomplete values |
| Same binding with `firmware_identity: "unknown"` or `firmware_version: "Unknown"` | Accepted | Check both identity and version for nonempty, unambiguous values, including case/whitespace variants |

All accepted probes still reported `qualification_state: unqualified` and
`physical_qualification_claim: false`; the defect is an overstated review-ready
gate, not a demonstrated physical qualification or printer-execution bypass.
The existing passing tests do not cover these negative cases. Closing them
requires adversarial regressions, review of both artifact contracts and their
consumers, and proof that complete printer-specific captures still pass only
for review. Do not repair the discrepancy by weakening the capture requirement.

Until those checks exist, manually inspect these inputs before treating a record
as ready for physical review. Physical capture can collect the missing evidence;
automated review readiness must not substitute for that inspection. This review
did not revalidate external manufacturer facts, authenticate captures, qualify
firmware dialects, or exercise a physical printer.

## Jared's next physical capture, per printer

For both the AD5M and AD5X, complete a separate worksheet and preserve the
original artifacts; do not pool evidence:

1. Photograph the nameplate/UI and record model, serial, stock/modded firmware
   identity, exact version, and any relevant machine configuration hash.
2. Record nozzle identifier, type, diameter, and installed state.
3. Record slicer version/profile identity and hash; capture the profile start
   program verbatim and hash it.
4. Export and preserve the actual start and end G-code as identified artifacts
   with SHA-256 values and record the actual coordinate convention (origin,
   units, absolute/relative modes). For AD5X also capture the real multi-material
   tool-change sequence. For AD5M record tool change as `not_applicable` with the
   single-material capability reason and no artifact/hash.
5. Measure toolhead left/right/front/back extents from the nozzle axis, lowest
   non-nozzle component, carriage height, cable/PTFE swept envelope at bed
   corners, rods/gantry/lid clearances, bed edges, tabs, and raised features.
6. For AD5X additionally measure the cutter, wiper/purge, and filament-handling
   obstacles across all applicable nozzle/tool states.
7. If a mesh is created, record measured mesh file/hash, coordinate transform,
   fixed versus moving components, uncertainty/inflation margin, photographs,
   scale bars/fiducials, and the nozzle/printer state. Do not use a guessed or
   image-inferred mesh as proof.
8. Record material manufacturer, type, color, lot, ambient temperature/humidity,
   operator, date/time/timezone, and the exact fields still blocking review.

These captures establish an evidence package for later review. They do not by
themselves establish physical qualification or complete the M2 exit criterion.
