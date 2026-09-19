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

## Isolated coupon package (software evidence)

The research-only package is `tools/m2_coupon.py`. It consumes an already
emitted, independently bound work packet and the actual versioned M2 proposal;
it does not slice, optimize an arbitrary job, or link into the production
export path. Its `prepare` action moves only complete packet path blocks and
fails closed unless the proposal is a complete dependency-safe permutation.
The packet must carry a `coupon_contract` for every path. That contract records
geometry, deposited volume, width, height, speed, temperature, fan, tool and
material. The pair verifier recomputes command content and these contracts; it
does not trust the generator's saved integrity report.

The canonical experiment contract is
`schema/m2-coupon-experiment.schema.json`; the explicitly synthetic/incomplete
example is `tests/predictive/m2_coupon_example.spec.json`. To create input
templates when a real printer package is unavailable:

```powershell
python tools/m2_coupon.py templates --out build/m2-coupon-input-templates
```

To create a physical bundle after Jared supplies real inputs and a rebuilt
independent verifier:

```powershell
python tools/m2_coupon.py prepare `
  --spec path/to/experiment.json `
  --gcode path/to/source-order.gcode `
  --machine path/to/AD5M.machine.json `
  --manifest path/to/source-order.manifest.json `
  --packet path/to/source-order.work-packet.json `
  --proposal path/to/m2-proposal.json `
  --model path/to/measured-model-fit.json `
  --verifier build/m2-trusted/jslice_verify.exe `
  --out build/m2-evidence/AD5M/experiment-001
```

The output directory is immutable and contains `baseline/` and `proposed/`
G-code, manifests and work packets, `experiment.json`, `machine.json`,
`model.json`, `proposal.json`, `manifest.json`, `pair-integrity.json`, trusted
verifier reports, `print-order.csv`, `raw-observations.csv`, a normalized
measurement template, `operator-checklist.md` and `README.md`. The command
refuses to overwrite an existing directory. `--permit-synthetic-test-fixture`
exists only for deterministic non-printable tests; it cannot qualify a physical
bundle.

The handoff commands after printing are:

```powershell
python tools/m2_coupon.py verify-pair --bundle build/m2-evidence/AD5M/experiment-001 --verifier build/m2-trusted/jslice_verify.exe
python tools/m2_coupon.py fit --measurements path/to/calibration-measurements.json --output path/to/model-fit.json
python tools/m2_coupon.py evaluate --bundle build/m2-evidence/AD5M/experiment-001 --measurements path/to/confirmatory-measurements.json --output path/to/evaluation.json
```

Calibration coupon IDs and confirmatory coupon IDs are disjoint in the
specification. The evaluator rejects overlap, unknown IDs, missing variants and
silently dropped exclusions. AD5M and AD5X must be evaluated as separate
experiments and must not be pooled. The preregistered rule is the rule in
`experiment.acceptance`: included proposed bond-proxy mean must improve by at
least `minimum_bond_proxy_improvement_percent`, while the recorded visible
quality and print-time guardrails remain within their registered limits. A
failed rule is M2 failed/revise; an incomplete or excluded record is not a
pass.

No real AD5M or AD5X machine fingerprint, serial/startup contract, material lot,
measured calibration dataset, or physical observation is present in this
repository. The existing `tests/verifier/machine.json` and
`tests/predictive/thermal_calibration.json` remain synthetic regression inputs
and are ineligible. Jared must supply, for each printer separately, model and
serial, firmware and startup assumptions, nozzle identity/diameter, material
family and lot, actual machine limits/tool envelope/heater/fan fingerprint,
ambient/bed/temperature/fan records, a measured calibration dataset and its
fit hash, the proposal/work-packet path contracts, instrument calibration,
randomized print records, raw thermography, destructive bond observations,
visible-quality/bridge/support scores, print times and exclusion reasons.

Automated evidence for this package is complete: the native M2 CTest run is
14/14, including the seven production fixture invariant/replay groups, thermal
schedule and calibration tests, 23/23 independent-verifier mutation groups,
8/8 coupon-package tests, and 14/14 machine-evidence tests. The standalone
verifier CTest is 2/2. The
production runner replays all 7/7 accepted fixtures. This is software evidence
only and does not satisfy the physical M2 exit criterion.

M2 remains **in progress**. No thermal schedule is enabled in ordinary slicer
exports, and no M3 work has started. General printer-changing lowering still
requires genuine paired AD5M and AD5X evidence, independent review of the
preregistered rule, and a later explicitly authorized milestone.

## M2.1 machine-evidence bootstrap

M2.1 adds `tools/machine_evidence.py`, the
`schema/machine-evidence.schema.json` contract, deterministic AD5M/AD5X draft
records, collision-geometry policy, and human-fillable printer worksheets. It
is evidence infrastructure only. Drafts remain `unqualified`; no physical
qualification claim is emitted. The tool preserves source conflicts and fails
closed on missing/ambiguous serial, firmware, startup, material, ambient,
operator, or coordinate evidence. See `M2_1_MACHINE_EVIDENCE.md`.

The existing M2 coupon gate now also requires, for non-fixture inputs, exact
firmware/profile and machine-artifact identities/hashes, required start/end
sequence artifacts, capability-dependent tool-change evidence, the actual
coordinate convention, material manufacturer/type/color, operator, and
observation date. AD5M must mark tool change `not_applicable` with its
single-material capability reason and no hash; AD5X must supply a real artifact
identity and SHA-256. The `--permit-synthetic-test-fixture` path remains test
only and cannot create physical evidence.
