# M2 thermal-debt scheduler status

M2 is in progress. This commit adds the analysis-only contracts and bounded
proposal machinery; it does not change preFlight path order or emit a different
G-code program. No physical calibration result exists, so M2's A/B exit
criterion is intentionally **not satisfied**.

The clean starting commit was
a644056b34fd2917a8b19de3f29566b91f20f628. Before editing, the M1 native
CTest run passed 9/9, the independent verifier CTest run passed 1/1, and
tools/trusted_verifier_tests.py passed 23/23 with seven accepted production
goldens and seven off/on equivalence checks.

## Contracts and trust boundary

Predictive/ThermalSchedule.hpp/.cpp consumes an immutable BeadGraphIR and a
machine fingerprint. It returns a versioned neighbor graph and a proposal with
baseline/proposed order, deterministic timing, dependency-safe reorderability
reasons, bounded search counters, score components, uncertainty and warnings.
The model contract records machine/material/process identity, calibration hash,
provenance, uncertainty and whether it is synthetic or calibrated. A model with
synthetic, inferred, missing or untrusted calibration data can produce
diagnostics but is never recommendation-eligible.

The spatial index is a deterministic grid over path bounds. Pair generation is
bounded by maximum_neighbor_pairs; path and pair IDs are sorted before output.
The scheduler uses source-order topological baseline timing and bounded adjacent
swap search. It keeps dependencies, object/instance/cancel-object boundaries,
material/tool identity, first-layer/skirt/brim, bridge, support and wipe
boundaries locked. Unknown roles and degenerate paths remain locked. This is a
conservative analysis rule, not a claim that final emitter state is known.

thermal_schedule.schema.json, thermal-model.schema.json,
thermal-model-fit.schema.json and thermal-calibration.schema.json are versioned
Draft 2020-12 contracts. The
M1.5 verifier accepts an optional thermal_schedule_proposal in a work packet
and independently checks that its path orders are complete permutations and its
precedence entries refer to existing sidecar paths in executable order. The
verifier does not evaluate thermal scores or make physical bond claims.

## Calibration and coupon evidence

tools/thermal_calibration.py fits an exponential cooling time constant from
complete coupon measurements and writes a deterministic fit artifact with
dataset SHA-256, process metadata, residual diagnostics and uncertainty.
Incomplete data, non-cooling measurements and out-of-domain temperatures are
rejected. tests/predictive/thermal_calibration.json is a synthetic example
fixture only; its fingerprint and measurements are explicitly not physical
evidence.

The planned AD5M/AD5X coupon pair uses identical printer, nozzle, material lot,
layer height, line width, temperature, fan and ambient metadata. Each pair
contains an unchanged preFlight-order coupon and an analysis-recommended-order
coupon, with contact-age thermography and a destructive bond proxy recorded
under the same fixture geometry. The preregistered record must include raw
CSV, normalized JSON, slicer commit, machine-fingerprint hash, model hash,
operator/time, camera/thermography calibration, visible-quality score and print
time. No result is present in this repository yet.

## Validation performed

Exact commands from this tree:

    cmd /c build\m2-native.cmd
    (the script calls VsDevCmd.bat -arch=x64 -host_arch=x64, then cmake -S . -B build/m2-native -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/src/j_slice/j-slice/deps/build/destdir/usr/local -DSLIC3R_GUI=OFF -DSLIC3R_PYTHON_PREPROCESSOR=OFF -DJS_SLICE_NATIVE_ADAPTER_TESTS=ON -DJS_SLICE_TRUSTED_VERIFIER=ON and cmake --build build/m2-native --parallel 8)
    ctest --test-dir build/m2-native --output-on-failure
    ctest --test-dir build/m1-native --output-on-failure
    ctest --test-dir build/trusted-verifier --output-on-failure
    python tools/trusted_verifier_tests.py build/m1-native/research/trusted_verifier/jslice_verify.exe --evidence build/m1_5-final-validation/verification-evidence.json --production-replay build/m1_5-final-validation
    python tools/predictive_golden.py run --manifest tests/predictive/jobs/manifest.json --baseline build/m2-native/src/Release/preFlight-console.exe --candidate build/m2-native/src/Release/preFlight-console.exe --comparator build/m2-native/research/predictive_core/predictive_compare_gcode.exe --accepted tests/predictive/production --output build/m2-qualification-2 --keep-going
    build/m1-native/research/predictive_core/predictive_thermal_schedule_tests.exe
    python tools/thermal_calibration_tests.py

The focused thermal executable passes, including deterministic graph/report
serialization, synthetic no-op eligibility, object/material/tool grouping,
dependency order, bridge/support locks, out-of-domain rejection and bounded
search. The native production adapter test also constructs real
ExtrusionEntity trees and runs the proposal API over the captured production
BeadGraphIR; it confirms deterministic synthetic no-op analysis. The calibration
test passes fit/provenance and incomplete-data rejection. The thermal proposal
serializer is validated against its Draft 2020-12 schema. The seven existing M1
production fixtures and ordinary G-code remain covered by the unchanged M1/M1.5
suites.

The remaining gate is physical, paired AD5M and AD5X coupon evidence. Until
those records are collected and reviewed, this code is infrastructure and
synthetic analysis only; it must not lower into printer-changing G-code.
