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

Supply observations in a JSON record with `fields` for the actual serial,
firmware identity/version, nozzle, slicer/profile and hash, exported start/end/
tool-change G-code, material manufacturer/type/color/lot, ambient conditions,
operator/date, and actual coordinate convention:

```powershell
python tools/machine_evidence.py bootstrap --model AD5X `
  --observations path/to/AD5X.observations.json `
  --output build/m2.1/AD5X.observed.json `
  --require-review-ready
python tools/machine_evidence.py validate --input build/m2.1/AD5X.observed.json --require-review-ready
```

`--require-review-ready` means the identity/export evidence is complete for
physical review; it never changes `qualification_state: unqualified` and never
sets a physical qualification claim. Unknown or ambiguous firmware, startup
behavior, or coordinates fails closed. A profile text/hash mismatch is rejected.

The optional `collision_geometry` object records coordinate frame, units,
nozzle-tip origin, mesh hash, transform, fixed and moving components,
printer/nozzle state, inflation margin, provenance, and qualification state.
Image-generated, inferred, community, or otherwise unqualified meshes cannot
satisfy exact collision-proof requirements.

## Jared's next physical capture, per printer

For both the AD5M and AD5X, complete a separate worksheet and preserve the
original artifacts; do not pool evidence:

1. Photograph the nameplate/UI and record model, serial, stock/modded firmware
   identity, exact version, and any relevant machine configuration hash.
2. Record nozzle identifier, type, diameter, and installed state.
3. Record slicer version/profile identity and hash; capture the profile start
   program verbatim and hash it.
4. Export and preserve the actual start, end, and tool-change G-code or
   immutable captures of each sequence, with hashes and the actual coordinate
   convention (origin, units, absolute/relative modes).
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
