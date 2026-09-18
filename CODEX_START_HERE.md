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

## In progress: M1 lossless adapter

Implement analysis-only conversion from preFlight's generated
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

M1 exit criterion is written in `IMPLEMENTATION_PLAN.md`.

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
- The future verifier must be independent and unable to repair paths.

## Recovery note

An earlier scratch checkout contained local commits `c85fdf5` and `7205ad3`, but
workspace maintenance removed their Git objects before they were persisted. This
tree is a clean reconstruction from the surviving design record, not a claim to
be byte-identical to those commits. Treat this repository's current commit as the
new authoritative baseline.
