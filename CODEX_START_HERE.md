# J-Slice Codex handoff

## Project

J-Slice is an artifact-first additive-manufacturing compiler built on preFlight.
Its primary result is a predicted manufactured artifact with uncertainty and
contract findings. G-code is the final lowering product.

Baseline upstream commit:

```text
92481fc5d2b60ba46abebe4d644284f037c67872
preFlight v1.2.0
```

Read in this order:

1. `docs/predictive-slicer/ARCHITECTURE.md`
2. `docs/predictive-slicer/IMPLEMENTATION_PLAN.md`
3. `docs/predictive-slicer/UPSTREAM_INTEGRATION.md`
4. `docs/predictive-slicer/FEATURE_LEDGER.md`
5. `docs/predictive-slicer/SUPPORT_COMPILER.md`
6. `docs/predictive-slicer/TRUSTED_VERIFIER.md`
7. `src/libslic3r/Predictive/`
8. `tools/predictive_core_smoke.cpp`

## Current state

M0/M0.1 are executable reference implementations for compiler interfaces,
contracts, deterministic scheduling, reduced forward models, bounded policy, and
temporary support topology. They are not calibrated production physics.

The code builds independently with C++20 and strict warnings. M1 now has native
entity capture, a source-order bead inventory, deterministic JSON, an opt-in CLI
analysis action and golden regression infrastructure. The native Windows headless
production build and all seven real slicing fixtures now pass off/on equivalence,
report determinism and schema checks. Source inventory qualification does not
resolve downstream emitter state; see the documented coverage limits.
Read `docs/predictive-slicer/M1_STATUS.md` and `tests/predictive/README.md` for the
implemented boundary, verification evidence and remaining work.

M1.5's contract/integrity boundary is complete in the separate
`src/TrustedVerifier` library and `jslice_verify` CLI. It consumes final G-code,
versioned machine/manifest files and a canonical work packet, replays its own
modal state, checks path precedence and conservative datum envelopes, and only
reports/rejects. Exact surface/part collision and dependency/contact physics are
deferred and explicit. Read `docs/predictive-slicer/M1_5_STATUS.md` and
`docs/predictive-slicer/TRUSTED_VERIFIER.md` before continuing. M2 is now in
progress as an analysis-only thermal-debt scheduler foundation. Read
`docs/predictive-slicer/M2_STATUS.md` and
`docs/predictive-slicer/M2_COUPON_PROTOCOL.md`; physical A/B
evidence is still required and no legacy emission replacement is permitted.
M2.1 adds the machine-evidence bootstrap and worksheets in
`docs/predictive-slicer/M2_1_MACHINE_EVIDENCE.md`; it remains unqualified
evidence infrastructure and does not alter ordinary output.

The canonical next-work record is
[M2.1 in the implementation plan](docs/predictive-slicer/IMPLEMENTATION_PLAN.md#m21-machine-evidence-bootstrap).
Before relying on review readiness, read the
[open validation findings](docs/predictive-slicer/M2_1_MACHINE_EVIDENCE.md#open-validation-findings)
and the physical-capture checklist in that same document. Passing the existing
software tests does not close those findings or the physical M2 gate.

## Qualified M1 source-inventory boundary

Preserve the analysis-only conversion from preFlight's generated
`ExtrusionEntity` trees into `BeadGraphIR` before G-code text emission.

Requirements:

- Do not change ordinary print output in M1.
- Preserve source order and all available object/layer/region/material/role data.
- Cover classic, Athena, Arachne, Serpentine, interlocking, infill, bridge,
  support, skirt, brim, and multi-material entities.
- Produce deterministic ArtifactIR JSON beside the unchanged G-code.
- Record downstream postprocessors that the analysis did not model.
- Add golden fixtures and a semantic G-code comparator before any optimizer is
  allowed to replace the legacy emission path.

M1 exit criterion is written in `docs/predictive-slicer/IMPLEMENTATION_PLAN.md`.

For M1.5, build `research/trusted_verifier` independently or enable
`JS_SLICE_TRUSTED_VERIFIER=ON` in the native root build. It never links into
the slicer. Exact native commands and the accepted/mutated program evidence are
in `docs/predictive-slicer/M1_5_STATUS.md`; the seven M1 inputs remain unchanged.

## Skill-assisted work

Use the skills installed in the current environment selectively. The repository
documents and tests remain sufficient when a skill is unavailable; do not copy
personal skill files or machine-specific installation paths into this tree.

| Skill | Useful question for J-Slice | Result belongs here |
| --- | --- | --- |
| atramentous | Which rationale, invariant, or unfinished gate would a future edit lose? | Existing area document, with a short local code link where needed |
| atra-review | Could the handoff cause someone to infer physical qualification or change only one side of a contract? | Findings with evidence and the smallest repair; review alone is read-only |
| atra-sweep | Do recorded memory links and lifecycle fields resolve and remain coherent? | Scoped audit; reconciliation when requested, with actual gate evidence |
| rubrica | What outcome, constraints, and acceptance tests define the next milestone change? | Existing implementation plan, not a parallel plan |
| apocrypha | Does a completion or review-readiness claim have evidence of the stated strength? | Corrected claim or an explicit open finding in the relevant status document |

For a consequential implementation, vitrail-review can combine a small selection
of relevant checks. Add cinereous when considering a shared contract abstraction;
add oxid for interrupted bundle creation, artifact ownership, or recovery risks.
Security review is appropriate for changes to parsers or untrusted input
boundaries. None is an automatic prerequisite for a documentation change.

For memory audits, scope inspection to J-Slice documents, Predictive,
TrustedVerifier, research targets, tools, and their tests. Exclude upstream and
vendored dependencies unless the task reaches them. A clean structural audit
does not establish correctness, test coverage, or physical readiness.

## Fast verification

Without CMake:

```bash
g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -Isrc \
  tools/predictive_core_smoke.cpp \
  src/libslic3r/Predictive/CompilerPasses.cpp \
  src/libslic3r/Predictive/ForwardModel.cpp \
  src/libslic3r/Predictive/PredictiveCompiler.cpp \
  src/libslic3r/Predictive/SupportPlanning.cpp \
  -o predictive_core_smoke
./predictive_core_smoke
```

With CMake:

```bash
cmake -S research/predictive_core -B build/predictive-core
cmake --build build/predictive-core
ctest --test-dir build/predictive-core --output-on-failure
```

For Visual Studio multi-configuration builds, add `--config Release` to the build
and `-C Release` to CTest. CMake also builds the adapter/comparator tests and checks
seven entity goldens. The direct g++ command above runs only the original smoke.

## Guardrails

- Keep all printer-changing features disabled by default until qualified.
- Preserve functional datums and contact upper/lower bounds.
- Do not describe executable seeds as validated physical predictors.
- Vision/language models may propose labels and priors, not silent numeric loads.
- Do not implement physical failed-part ejection before safe object exclusion.
- Free-space support strands/arches remain rejected until experiments establish
  their process envelope.
- The verifier must remain independent and unable to repair paths.

## Recovery note

An earlier scratch checkout contained local commits `c85fdf5` and `7205ad3`, but
workspace maintenance removed their Git objects before they were persisted. This
tree is a clean reconstruction from the surviving design record, not a claim to
be byte-identical to those commits. Treat this repository's current commit as the
new authoritative baseline.
