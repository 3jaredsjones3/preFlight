# M1 implementation and Windows qualification

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

## Capture boundary and downstream transformations

Capture remains after `Print::process()` and before `export_gcode()`. These
downstream operations are absent from the source inventory, even when their
final G-code is covered by off/on equivalence:

- G-code emission chooses object/extrusion ordering, effective tools (including
  wipe-into-object overrides), speeds, acceleration, temperatures and firmware
  cancel labels. The source object/instance provenance is retained; emitted
  labels and their exact command positions are checked in the final program.
- Seam placement, scarf seams, loop splitting/clipping, smooth-path construction,
  arc fitting, dynamic overhang segmentation and interlocking gap-flow
  adjustments occur during emission. Source widths and `mm3_per_mm` are captured;
  final machine flow and seam coordinates are not predictions in the report.
- Travel, retraction, wipe motions, custom G-code, generated wipe-tower programs
  and layer/tool-specific skirt/brim expansion are not extrusion-tree leaves.
- The `GCodeGenerator::process_layers` pipelines apply optional spiral-vase and
  pressure-equalizer transformations, cooling/fan control and optional find/replace.
  The named arc-handler stage currently passes through; smoothing/arc fitting
  happens earlier in path construction.
- `GCodeProcessor::finalize(true)` resolves statistics and time/progress
  substitutions, and can invoke Python preprocessing in builds with that option.
  The CLI subsequently runs configured external postprocessors and can encode
  binary G-code. The M1 fixtures use text G-code and no user postprocessors.

References: `src/libslic3r/GCode.cpp` (`do_export`, `process_layers`,
`extrude_perimeters`), `src/libslic3r/GCode/GCodeProcessor.cpp`,
`src/libslic3r/GCode/LabelObjects.cpp`, and `src/CLI/ProcessActions.cpp`.

## Windows dependency and build commands (2026-09-18)

Starting commit: `f10fe97` on `j-slice-development`. The original Boost failure
was an unbootstrapped application configuration with no dependency prefix. The
repository requires Visual Studio 2026, and its supported `win_amd64` bootstrap
builds Boost **1.90.0** from the pinned, SHA-256-checked source recipe (the
application's minimum is 1.83). No additional package manager was introduced.

Toolchain: Visual Studio 2026 Community 18.6.2, MSVC 19.51.36246 / toolset
14.51.36231, VS-bundled CMake 4.2.3-msvc3 and Ninja. Validation uses the existing
Python 3.13.1 installation with jsonschema 4.24.0.

From PowerShell in `C:\src\j_slice\j-slice`:

```powershell
.\build_deps.bat -preset win_amd64 *> build/m1-deps-bootstrap.log
```

This uses Git Bash, `build_deps.sh`, `deps/CMakePresets.json`, the Visual Studio
18 2026 x64 generator and the repository's pinned dependency recipes. The entire
Release dependency target succeeded. The optional subsequent Debug build was
stopped at Boost extraction by terminating this task's bootstrap Bash process
tree; no Release library was removed. Release installation:
`C:/src/j_slice/j-slice/deps/build/destdir/usr/local`, also recorded by the script
in `deps/build/.DEPS_PATH.txt`.

The supported up-to-date dependency check succeeded:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build deps/build --target deps --parallel 1
```

Do **not** add `--config Release` to this dependency command: the wrapper's
`CMAKE_BUILD_TYPE=Release` already selects the external libraries' configuration.
Changing the parent Visual Studio configuration selects different ExternalProject
stamps. An initial verification attempt with that override began re-extracting
Boost and was stopped; the command above matches `build_deps.sh` and completed
without rebuilding dependencies. Logs: `build/m1-deps-release-check.log` (stopped
attempt) and `build/m1-deps-supported-check.log` (successful check).

Production configuration/build, from a Windows command prompt in the repository
(executed here through `build/m1-configure.cmd` and `build/m1-build.cmd`):

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build/m1-native -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/src/j_slice/j-slice/deps/build/destdir/usr/local -DSLIC3R_GUI=OFF -DSLIC3R_PYTHON_PREPROCESSOR=OFF -DJS_SLICE_NATIVE_ADAPTER_TESTS=ON
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build/m1-native --parallel 8
```

`build/m1-native` began empty. This is the production headless engine/CLI with
STEP support and Release LTO, not the GUI or embedded Python-preprocessor build.
`JS_SLICE_NATIVE_ADAPTER_TESTS` now registers the six independent test groups and
the native adapter and declaration-order tests in the same production CTest tree. Build logs are
`build/m1-native-configure.log` and `build/m1-native-build.log`.

The first native build compiled all M1 sources and the native test, then failed
at the production DLL link: `NSVGUtils.cpp` needs NanoSVG, but upstream only
linked that parser through the GUI. `libslic3r` now declares its own NanoSVG
dependency, allowing the supported headless configuration to link. The initial
link also exposed libjpeg-turbo's `/MT` default (`LNK4098`); the repository's JPEG
recipe now selects `WITH_CRT_DLL=ON` on MSVC to match the application's `/MD`.
Neither fix changes an extrusion generator or emitter.

## Verification commands and results

After the JPEG recipe change, the supported dependency target and final build ran:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build deps/build --target dep_JPEG --parallel 1
.\build\m1-configure.cmd
.\build\m1-build.cmd
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build/m1-native --verbose
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build/predictive-core --config Release
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build/predictive-core -C Release --output-on-failure
```

The `.cmd` files are local wrappers for the complete commands above, not required
repository inputs. The separate pre-existing `build/predictive-core` uses VS 2022;
the fresh production build and its copy of every predictive target use VS 2026.

- Production Release build succeeded: `preFlight-console.exe`, `preFlight.dll`,
  application wrappers and `OCCTWrapper.dll`, plus all predictive test tools.
  `LegacyExtrusionAdapter`, `LegacyPrintAnalysis`, `ArtifactJson` and
  `CanonicalGCodeComparator` compiled in the native engine. Headless `--help`
  and real FFF slicing/export succeeded. Final link has no `LNK4098`.
- Original independent suite: **6/6 groups passed**. Production suite:
  **8/8 groups passed**, adding native entity capture and legacy label ordering.
  The runner group contains **15 Python tests**; seven synthetic entity goldens
  retain provenance/geometry/volume checks and Draft 2020-12 schema validation.
- Native composite tests additionally preserve real loop/multipath segment
  boundaries, differing widths/heights/flow, bridge roles, supplied object/cancel
  provenance and source nonmutation. Existing oriented-path/flow-ratio tests pass.
- Ordinary upstream compile warnings remain (including incomplete Python object
  deletion with the Python option off); this is not a warning-free GUI build.
  The isolated predictive C++ target remains strict `/W4 /WX /permissive-`.

Logs retained locally: `build/m1-jpeg-runtime.log`,
`build/m1-native-final-build.log`, `build/m1-native-final-configure.log`,
`build/m1-native-verification-build.log`, `build/m1-native-ctest-final.log`,
`build/m1-isolated-build.log`, `build/m1-isolated-ctest.log` and
`build/m1-runner-tests.log`. CTest logs are also saved in
`tests/predictive/production/` so the verification summary survives build cleanup.

The final fixture commands were:

```powershell
python tools/predictive_fixture_jobs.py
python tools/predictive_golden.py run --manifest tests/predictive/jobs/manifest.json --baseline build/m1-native/src/Release/preFlight-console.exe --candidate build/m1-native/src/Release/preFlight-console.exe --comparator build/m1-native/research/predictive_core/predictive_compare_gcode.exe --output build/m1-offset-qualified --keep-going
python tools/predictive_golden.py run --manifest tests/predictive/jobs/manifest.json --baseline build/m1-native/src/Release/preFlight-console.exe --candidate build/m1-native/src/Release/preFlight-console.exe --comparator build/m1-native/research/predictive_core/predictive_compare_gcode.exe --accepted tests/predictive/production --output build/m1-accepted-replay --keep-going
python tools/legacy_label_gcode_regression.py --slicer build/m1-native/src/Release/preFlight-console.exe --comparator build/m1-native/research/predictive_core/predictive_compare_gcode.exe --output build/m1-label-qualification
```

Output directories must be new on rerun. Each suite exports all seven jobs four
times: two ordinary exports and two analysis exports, using the **same production
DLL**. This establishes off/on equivalence and repeatability; it does not claim
comparison against a separate pre-`f10fe97` binary. Each evidence record includes
exact slicer argv, input hashes, launcher/comparator hashes, actual engine/runtime
DLL hashes, G-code/report hashes, observed coverage and comparison results.

The first passing suite contributed 28 exports, followed by another 28 against
explicitly reviewed, compressed G-code and JSON goldens. Both suites passed:
**56 production exports, 28 schema-valid reports, all seven report pairs
byte-identical in each suite and exact matches to the accepted JSON on replay**.
All 42 within-suite G-code comparisons passed: 38 were raw byte-identical; four
differed only in the reviewed generation timestamp. All seven saved G-code
baselines also passed replay. After removing that single timestamp comment, even
the raw remaining bytes matched in every case; semantic normalization did not
hide changed commands. Ordinary exports created no sidecar.

The sole ignored prefix is `; generated by preFlight 1.2.0 on `, established by
reviewing the complete first production diff (one changed timestamp line).
The C++ comparator is unchanged. No object comments, roles, settings, motion,
extrusion, tool, temperature, feedrate, seam or support commands are excluded.
Thus all executable order, effective flow and in-body cancel boundaries in these
fixtures remain exact. This is equivalence evidence, not physical certification.

## Observed production fixture matrix

Counts refer to actual captured leaf paths, not role enum availability. All rows
passed ordinary/repeated-analysis comparison, report determinism, schema and
accepted-baseline replay. `Raw` counts comparisons to the first ordinary export
out of three in the first passing suite; the remaining comparisons differ only
in generation time.

| Fixture | Source paths | Observed native/emitted coverage | Raw |
| --- | ---: | --- | ---: |
| mechanical | 189 | Athena: 100 perimeter, 84 fill, 1 skirt and **4 brim** paths | 0/3 |
| thin_wall | 31 | Arachne: 30 perimeter leaves, two widths, external-perimeter output; 1 skirt | 3/3 |
| bridge_support | 391 | 180 perimeter, 125 fill, 85 support (76 support + 9 interface), 3 bridge-infill leaves, 37 oriented paths; emitted bridge/support/interface | 3/3 |
| multi_object | 181 | Objects 0 and 1, 100 perimeter + 80 fill + skirt; four distinct start/stop labels and unchanged boundaries | 3/3 |
| serpentine | 4861 | 4860 Serpentine leaves in 30 real multipaths + skirt; emitted Serpentine roles | 3/3 |
| interlocking | 166 | 72 interlocking perimeter leaves among 132 perimeters, 33 fills + skirt; 24 emitted interlocking sections | 3/3 |
| multi_material | 211 | 120 perimeter + 90 fill + skirt; PLA/PETG and tools 0/1 in the report; actual T0/T1 switches and 63 wipe-tower sections | 3/3 |

Replay raw comparisons were 3/3 for every row except thin_wall (2/3). All remaining
bytes after timestamp removal match, including the archived baselines. Full
counts and hash evidence: `tests/predictive/production/evidence.json` and
`replay.json`. Actual artifacts and ordinary G-code are stored alongside them;
the runner never updates accepted files.

Fixture corrections were necessary before acceptance: explicit `outer_only`
activates brim generation (width alone produced no brim); the wipe-tower profile
requires relative E and `layer_gcode = G92 E0`; both configured tool offsets are
now explicit. Production coverage assertions reject metadata-only generator
claims, empty brim containers and configured tools without emitted tool changes.

## Pre-existing upstream determinism defects discovered during M1

**Independent OctoPrint declarations.** Disabled/disabled exports originally
failed because `LabelObjects::init` grouped header declarations by pointer address.
With explicit user authorization, commit
`cf89ba5c1d021dccaee61cdaf17d4a5afe439a79` changes only the ordering of independent
OctoPrint declarations to existing model object/instance indices. Label names,
unique IDs and in-body lookup are unchanged. Firmware header/ID order is untouched.
The focused native regression was demonstrated failing before the fix with no
predictive API. Ten actual exports (five off/five on) passed afterward, including
an audit that every historical executable byte and body label remained unchanged.
The unchanged comparator still rejects the archived original disabled pair.
Evidence and instructions: `tests/regressions/object-declarations/README.md`.
This is a separate commit from M1 adapter/build qualification.

**Underspecified tool offsets.** With two nozzles but only the default one-element
`extruder_offset`, disabled/disabled wipe-tower moves differed (first difference
X4.200 versus X4.400). `WipeTowerIntegration` copies the config vector and indexes
`m_extruder_offsets[tcr.initial_tool/new_tool]` without a size check. The captured
programs have T1 and only `extruder_offset = 0x0`. This is an upstream unchecked
configuration assumption, not an analysis mutation. The fixture now supplies
`extruder_offset = 0x0,0x0`; the same binary then passed all comparisons. No emitter
or motion behavior was patched and the failing input/output evidence is retained
under `tests/regressions/underspecified-tool-offsets/`. Arbitrary underspecified
multi-tool CLI profiles remain an upstream limitation outside this qualification.

## Exit criterion and remaining limits

**The written M1 end-to-end equivalence exit criterion is satisfied for the seven
pinned fixtures on this native Windows production build.** Analysis traverses real
post-process extrusion trees into the new IR, writes deterministic schema-valid
JSON and leaves final G-code exact except the documented timestamp comment.
The production-build/equivalence portion of M1 is complete. No M1.5 verifier,
thermal scheduling or printer-changing optimization was introduced or enabled.

This qualifies an analysis-only **source inventory**, not a lossless inventory of
the fully emitted job. The following remain explicit coverage/semantic limits:

- **Classic generator unavailable:** upstream `LayerRegion::make_perimeters`
  dispatches only Athena/Arachne; `PerimeterGeneratorType` offers no classic
  choice. Common loop/path representation is natively tested, but no classic
  production-generator coverage is claimed.
- Interlocking perimeter generation is covered. These fixtures generated no
  `interlocking_gap_fills` leaves and only flow ratio 1.0; native tests preserve
  non-unit flow ratio 1.5. That separate gap-fill generation branch is unqualified.
- OctoPrint object labels and a native two-object/two-instance permutation fixture
  are covered. Firmware M486 label order is protected by a native regression,
  but cross-process firmware-label determinism, Klipper, sequential-object mode
  and every instance arrangement are not qualified by the seven slicing jobs.
- Speed, seam position, effective tool overrides, resolved firmware cancel label,
  auxiliary repetitions and generated wipe-tower moves are downstream, as
  enumerated above. They are absent or explicitly unknown in JSON; source object/
  instance/layer/region identities and available raw attributes remain preserved.
  Wipe-tower equivalence is verified in G-code, not claimed as entity capture.
- GUI, embedded Python preprocessing, external postprocessing scripts, binary
  G-code, spiral vase and every optional emitter transformation are not tested
  by this headless/text-G-code fixture set. Their report limitations remain.
- No thermal/bond/duration predictions, calibrated machine fingerprint, complete
  machine-state inventory or safety certification follow from these tests.

There is no remaining dependency/build or seven-fixture equivalence blocker.
The underspecified-offset case and unqualified branches above must not be
represented as covered or resolved by this work.
