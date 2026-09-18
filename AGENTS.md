# J-Slice repository instructions

## Scope

This is a preFlight-derived research fork for the J-Slice predictive additive
manufacturing engine. Preserve upstream behavior while introducing a composable,
artifact-first compiler.

## Before editing

- Read `CODEX_START_HERE.md` and the architecture/implementation documents it
  routes to.
- Inspect the real upstream types and call sites before proposing adapters.
- Check `git status`; preserve unrelated user work.

## Engineering rules

- Use C++20 and keep the isolated predictive target warning-clean.
- Prefer deterministic algorithms and stable ordering.
- Preserve provenance through every transformation.
- A compiler pass must declare the contracts it reads, changes, and protects.
- Uncertainty and unmodeled downstream behavior must appear in reports.
- Unknown or unsafe final machine state fails closed at the verifier boundary.
- Keep experimental physical assumptions behind disabled feature gates.

## Milestone discipline

- M1 is analysis-only and must not change legacy G-code.
- Add regression tests before replacing legacy lowering.
- Do not combine adapter, optimizer, and verifier work into one opaque change.
- Commit by milestone with a verification summary.

## Verification

Run the independent smoke build described in `CODEX_START_HERE.md`. When the full
preFlight dependency toolchain is available, also build the affected production
targets and run relevant upstream tests.
