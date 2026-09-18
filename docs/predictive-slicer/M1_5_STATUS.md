# M1.5 implementation status

Starting commit: `9228575df549161ff77f6b103580aa16c2f2d91a`.
Working tree was clean before any modification. Before editing, native CTest
passed all eight groups, then the full M1 accepted-baseline replay passed all
seven fixtures (28 exports, 14 deterministic/schema-valid reports).
Evidence: `build/m1_5-start-baseline/evidence.json`.

Commands run before modification:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build/m1-native --output-on-failure
python tools/predictive_golden.py run --manifest tests/predictive/jobs/manifest.json --baseline build/m1-native/src/Release/preFlight-console.exe --candidate build/m1-native/src/Release/preFlight-console.exe --comparator build/m1-native/research/predictive_core/predictive_compare_gcode.exe --accepted tests/predictive/production --output build/m1_5-start-baseline --keep-going
```

## Outcome and scope

The independent v1 static verifier is implemented. Its native standalone and
production-tree builds pass the initial acceptance/mutation gate. The **full
M1.5 milestone is still in progress**: exact datums, deposited-part collision,
dependency/contact contracts and authenticated artifact/calibration binding are
unproven. The original broad exit criterion has not been replaced with a weaker
one. No thermal scheduling, replacement lowering or printer-changing optimization
was introduced. No slicer/adapter/comparator implementation changed.

## Architecture and changed files

- `src/TrustedVerifier/Verifier.{hpp,cpp}`: independent modal lexer/replay,
  static checks, deterministic line/byte-addressed JSON findings. Inputs are const
  byte strings; there is no repair, write, generator or machine-control API.
- `src/TrustedVerifier/Input.cpp`: strict JSON/schema validation and system
  SHA-256. Versioned schemas are embedded at build time, not loaded from an
  untrusted path at verification time. Duplicate/unknown keys and unsupported
  versions reject.
- `tools/trusted_verify.cpp`: read-only three-input CLI; JSON stdout, exit 0/1/2.
- `research/trusted_verifier/`: separate library/executable and strict C++20
  build. `CMakeLists.txt` adds opt-in separate root targets only.
- `schema/verifier-machine.schema.json`, `verification-manifest.schema.json`,
  `verifier-report.schema.json`: independent `js-machine-1`, `js-verification-1`
  and `js-verifier-report-1` contracts. The M0 fingerprint schema is unchanged.
- `tools/trusted_verifier_tests.py`, `verifier_fixture_manifests.py`, and
  `tests/verifier/`: explicit trusted regression inputs, twenty production
  mutations, modal/schema/failure tests and persistent evidence.
- `TRUSTED_VERIFIER.md`, `IMPLEMENTATION_PLAN.md`, `FEATURE_LEDGER.md`,
  `ARCHITECTURE.md` and `CODEX_START_HERE.md`: scope and handoff updates.

The verifier links only nlohmann JSON 3.12.0 from the repository's existing
dependency installation and system BCrypt on Windows. No new package manager or
arbitrary binary was used. Non-Windows builds select OpenSSL Crypto but are not
qualified here. The CMake link contract, compile inputs and Windows `dumpbin`
inspection contain no generator/optimizer/comparator library. The executable's
DLL dependencies are Windows/VC runtime plus `bcrypt.dll`.

The root project targets Windows 8 APIs. Its first verifier build exposed that
the one-shot BCryptHash API is newer, so hashing uses the compatible
CreateHash/HashData/FinishHash sequence instead. The verifier directory removes
the parent's `/W3` and conversion-warning suppressions locally and compiles
under `/W4 /WX /permissive-`. This does not change slicer compile flags.

## Exact build and test commands

All commands ran from `C:\src\j_slice\j-slice`, using VS 2026 Community 18.6.2,
MSVC 19.51.36246, bundled CMake 4.2.3-msvc3/Ninja, Python 3.13.1 and jsonschema
4.24.0. Existing M1 dependencies at `deps/build/destdir/usr/local` were reused.

Standalone configure/build from a command prompt (local helper
`build/m1_5-configure.cmd` contains these commands):

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S research/trusted_verifier -B build/trusted-verifier -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/src/j_slice/j-slice/deps/build/destdir/usr/local -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DJS_VERIFIER_PRODUCTION_REPLAY=C:/src/j_slice/j-slice/build/m1_5-start-baseline
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build/trusted-verifier --parallel 4
```

Production-tree configure/build (local helper `build/m1_5-native.cmd`):

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build/m1-native -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/src/j_slice/j-slice/deps/build/destdir/usr/local -DSLIC3R_GUI=OFF -DSLIC3R_PYTHON_PREPROCESSOR=OFF -DJS_SLICE_NATIVE_ADAPTER_TESTS=ON -DJS_SLICE_TRUSTED_VERIFIER=ON -DJS_VERIFIER_PRODUCTION_REPLAY=C:/src/j_slice/j-slice/build/m1_5-final-baseline
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build/m1-native --parallel 8
```

Fixture generation, production replay and verification from PowerShell:

```powershell
python tools/verifier_fixture_manifests.py
python tools/predictive_golden.py run --manifest tests/predictive/jobs/manifest.json --baseline build/m1-native/src/Release/preFlight-console.exe --candidate build/m1-native/src/Release/preFlight-console.exe --comparator build/m1-native/research/predictive_core/predictive_compare_gcode.exe --accepted tests/predictive/production --output build/m1_5-final-baseline --keep-going
python tools/trusted_verifier_tests.py build/trusted-verifier/jslice_verify.exe --evidence build/trusted-verifier/verification-evidence.json --production-replay build/m1_5-start-baseline
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build/trusted-verifier --verbose
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build/m1-native --output-on-failure
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build/predictive-core --config Release
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build/predictive-core -C Release --output-on-failure
& 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\dumpbin.exe' /DEPENDENTS build/trusted-verifier/jslice_verify.exe
```

M1 replay output directories must be fresh on subsequent runs. The CLI invocation
for a caller's three already-bound input files is:

```text
jslice_verify PROGRAM.gcode MACHINE.json MANIFEST.json
```

No output G-code is produced. The test runner supplies actual input paths and
captures the JSON report from stdout.

## Final verification results

- Original independent predictive suite: **6/6 CTest groups passed**.
- Native production tree: **9/9 CTest groups passed**, preserving the original
  eight and adding the verifier suite.
- Standalone verifier: **21/21 unittest groups passed, zero skips**, also exposed
  as one CTest group. The production-tree verifier passed the same 21 groups.
- **7/7 archived production goldens accepted**, deterministic on repetition;
  **20/20 focused production mutation cases rejected for their intended codes**.
- **28/28 final production exports accepted** under the synthetic fingerprint,
  with equivalent off/on results per fixture. Every report is schema-validated.
- Seeded independent decimal replay: **692 commands matched**, including unit,
  coordinate/extrusion-mode and reset transitions. A separate two-fault test
  confirms that a later invalid tool is still parsed and diagnosed after an
  unsupported command; full-file counters alone do not establish this.
- Both initial and final M1 accepted-golden suites passed **7/7 fixtures**
  (28 exports and 14 analysis reports each). All production runtime DLL hashes
  were unchanged between them; the verifier is not linked into the slicer.

Persistent evidence is under `tests/verifier/evidence/`: `m1-start.json`,
`m1-final.json`, `native-qualification.json`, `standalone-qualification.json`,
CTest/build logs, `link-contract.txt` and `native-dependencies.txt`. It records
exact executable/input hashes, findings, mutation lines, final states and
off/on result equivalence. Native link rules list only `Verifier.cpp.obj` and
`Input.cpp.obj` in the verifier library, then that library plus the CLI object
in the executable. No generator implementation participates.

## Accepted baselines and machine trust

Seven archived M1 production programs are accepted, repeated deterministically.
All 28 actual production exports (ordinary baseline, ordinary repeat, analysis
and analysis repeat across seven fixtures) receive equivalent verifier results.
Only the report's explicit input hashes differ when timestamp bytes differ;
findings, state, metrics and acceptance must otherwise be identical.

Both the pre-edit and final M1 replay pass all seven fixtures against saved G-code
and exact analysis JSON. Analysis remains opt-in and ordinary output is unchanged.
The synthetic verifier fingerprint binds a 300 mm box, 200 mm/s maximum feed,
XYZ speed caps of 200 mm/s, E cap 120 mm/s, 200 mm3/s positive drive-flow cap,
two logical tools sharing one heater, and a small axis-aligned tool envelope.
It is explicitly **not a calibrated printer profile**. M1 unretractions reach
about 96.2113 mm3/s of positive drive flow, which v1 conservatively includes.

| M1 golden | Synthetic 300 mm fingerprint | Original 220 mm box check |
| --- | --- | --- |
| mechanical | Accepted | Accepted |
| thin_wall | Accepted | Accepted |
| bridge_support | Accepted | Accepted |
| multi_object | Accepted | Accepted |
| serpentine | Accepted | Accepted |
| interlocking | Accepted | Accepted |
| multi_material | Accepted | **Rejected: motion.bounds, line 204** |

The last row is a pre-existing safety limitation revealed by independent replay:
the unchanged wipe tower extends beyond the M1 fixture's 220 mm profile. M1's
equivalence qualification never established printer safety. The 300 mm acceptance
suite is an explicit virtual-machine test, not a claim that the original 220 mm
job became safe. Startup `(10,10,0)`, no hidden firmware transforms and maintained
temperatures are trusted assumptions; the goldens contain no homing command.

## Focused production mutation matrix

Each row starts from an accepted archived M1 program. Physical/syntax mutations
receive a freshly bound program hash so rejection cannot be attributed to generic
hash mismatch. All lines and command lines must be counted, reports repeated
byte-identically, and **the complete set of error codes must equal the intended
code**. Line assertions point to the mutation when applicable. Schema failures
are separately tested at line 0, before any untrusted startup state is consumed.

| Mutation | Required rejection |
| --- | --- |
| X beyond machine box | `motion.bounds` |
| Nozzle inside box but tool envelope outside | `tool.envelope` |
| Excessive feed | `motion.feedrate` |
| Excessive E-axis speed below global feed cap | `motion.axis_velocity` |
| Excessive volumetric flow below E-axis cap | `extrusion.flow` |
| NaN coordinate | `numeric.nonfinite` |
| Infinite coordinate | `numeric.nonfinite` |
| Positive E after a wait below minimum temperature | `extrusion.cold` |
| Parameters on coordinate-mode transition | `modal.parameters` |
| Remove both XYZ mode declarations | `modal.unknown` |
| M302 cold-extrusion override | `command.unsupported` |
| Invalid tool T9 | `tool.invalid` |
| Excess temperature | `temperature.bounds` |
| Impossible single E discontinuity | `extrusion.continuity` |
| Excess configured acceleration | `motion.config_limit` |
| Unsupported arc | `arc.unsupported` |
| Cumulative unrecovered retraction | `extrusion.retraction` |
| Wrong fingerprint hash | `binding.fingerprint` |
| Truncate terminal marker | `program.truncated` |
| Mismatched object end | `object.boundary` |

Supplemental tests cover JSON/schema mismatch, duplicate fields, unknown input
keys, program hash mismatch, trailing commands, heater shutdown, duplicate object
IDs, header corruption, G92 physical offsets and retained retraction, heating
without waiting, fans, units, G90 clearing E override, configured axis limits,
speed backup/restore, E-only flow overrides, nonzero tool offsets and required
unproven properties. A second parser using decimal arithmetic checks seeded modal
sequences independently of the C++ implementation. Targeted malformed-input
tests are present; coverage-guided fuzzing is not claimed.

## Proven, partial and unproven invariants

Within the supplied dialect, immutable input and startup assumptions, v1 checks
syntax, modal/numeric validity, convex build-box bounds, translated AABB envelope
bounds, requested feed/axis/volumetric-flow limits, configured acceleration limits,
temperature targets and minimum-temperature state, E continuity/retraction bounds,
exact object-label consistency, byte hash binding and schema compatibility.

These checks are partial physical evidence: they do not reconstruct acceleration
trajectories, actual temperatures, bead geometry, filament presence or deposited
matter. Every report lists unproven datum protection, swept part collision,
dependency/contact contracts, physical state, dynamic motion and ArtifactIR
binding. Requiring any of these in the manifest rejects with `rule.unproven`.
No unproven property is reported as certified merely because there are warnings
instead of errors. The result's scope is `conditional_static_checks`.

Unsupported commands fail closed, including G2/G3, homing/leveling, firmware
retraction, workspaces, volumetric E, jerk changes, nonzero pressure advance,
firmware/Klipper object exclusion and macros. The complete supported command
table and trust contract are in `TRUSTED_VERIFIER.md`.

Remaining M1.5 work: exact geometry/forbidden-region contracts, independent
deposited-part reconstruction and collision, support/dependency/contact evidence,
authenticated fingerprint/artifact/contract packages, more command dialects,
arcs/homing/tool compensation, coverage-guided fuzzing and report signatures.
Thermal scheduling and predictive G-code changes remain out of scope.
