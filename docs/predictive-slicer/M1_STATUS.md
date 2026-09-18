# M1 first implementation slice

## Implemented

- Read-only native capture of collections, empty collections, loops, multipaths,
  ordinary paths and oriented paths, including `no_sort`, reversal permission,
  loop role, full 16-bit extrusion role, raw integer XY and all path attributes.
- Dependency-free snapshot-to-`BeadGraphIR` conversion in source preorder. Each
  path segment retains its own width/height/flow; shared junctions are not merged.
  Incoming segment volume uses `mm3_per_mm` exactly once. It does not multiply the
  interlocking `flow_ratio` again or infer area as width times height.
- Stable sequential source/path IDs and parent/child indices. Raw source entity
  records retain exact integer coordinates even where double-valued bead geometry
  loses precision. Empty/degenerate/unsupported input is retained or reported.
- Print inventory traversal across objects, instances, layers, regions, perimeter
  and fill island ranges, interlocking gap fills, support, skirt and brim. Thin
  fills are already copied into `fills()` upstream and are not double-counted.
- Opt-in CLI action `--predictive-analysis` performs ordinary export and writes
  `<final-output-path>.artifact.json`. Capture happens after `Print::process()` and
  before `export_gcode()`. The legacy G-code generation and postprocessing calls
  are unchanged. The new branch invokes no optimizer or replacement emitter.
- Deterministic, locale-independent JSON with explicit null predictions and
  unmodeled downstream passes. Separate `m1-analysis-1` schema; it intentionally
  does not fabricate numbers required by the M0 predicted-artifact schema.
- Seven synthetic entity goldens, comparator mutation checks, a native entity
  test target, seven synthetic AMF slicing jobs and a four-run regression harness.

## Contracts and interpretation

This pass reads generated extrusion trees and contextual print metadata, changes
only analysis-owned data, and protects source geometry, ordering, attributes,
identity, and legacy output. The result is a source inventory, **not a machine
schedule**. No dependency edges or physical predictions are invented.

Object/instance identifiers are model-vector indices, not process-global ObjectID
counters or pointer addresses. Print-object and layer/region indices supplement
them. Island is the layer's flattened slice/island ordinal where explicit ranges
exist. `configured_tool` is zero-based and `material` is the configured filament
type. These are not claims about later wipe-into-object tool overrides. Unknown
support current-tool selection stays null. The caller may supply a known cancel
label or speed through `LegacyContext`; whole-print capture leaves those null.

Skirt/brim records are generated templates. They have unknown layer/Z/tool and
are not expanded over every emitted layer. Their derived bead Z=0 is a placeholder;
the authoritative source context has `print_z_mm: null`. `closed` is per-path
geometric closure; whole-loop topology is retained by its parent entity record.
Entity kind and coarse role ordinals are defined by their C++ enums and versioned
with the report schema. Samples are `[x,y,z,width,height,incoming_volume]`.

`BeadGraphIR::analysis_only` blocks these unresolved inventories at the predictive
compiler's validation boundary. Ordinary M0 research graphs retain their existing
behavior. This guard does not qualify the future independent M1.5 verifier.

## Still required for M1 completion

- Compile native adapter, Print traversal and CLI integration in a dependency-
  complete preFlight build, then run the native test and full slicing suite.
- Save accepted production G-code baselines and demonstrate ordinary/analysis
  output identity across the seven jobs. Synthetic snapshot tests do not prove it.
- Capture emitter-resolved order, speed, seam, firmware cancel labels, effective
  tool/material, auxiliary repetitions and wipe-tower program if needed for a
  complete emitted-job inventory. Report remaining unknowns until then.
- Bind source/settings/build/fingerprint manifests before claiming reproducible
  physical prediction; fit models before supplying thermal or bonding estimates.

## Verification in this workspace

The workspace originally contained only the overlay and recovery patch. A new
`j-slice/` checkout was restored at upstream `92481fc5d2b60ba46abebe4d644284f037c67872`;
the supplied recovery patch was applied as `a766a677fc83e8387d6291e3f5407e0c24635c30`.
The original files remain untouched. `upstream` points to `oozebot/preFlight`.

The isolated C++20 target builds with MSVC `/W4 /WX /permissive-`. Core smoke,
adapter/comparator tests, seven JSON golden checks, AMF input validation and JSON
Schema validation pass. Runner tests also reject changed extrusion, missing or
nondeterministic reports and missing feature coverage using a synthetic slicer
and the real comparator. The full headless configuration was attempted with GUI
and Python preprocessing off, but stopped because Boost 1.83 development packages
are unavailable. Native linking, slicing jobs and output-equivalence qualification
therefore remain unverified. M1 is in progress, not complete.
