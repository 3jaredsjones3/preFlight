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

## Project memory and optional skills

Use `CODEX_START_HERE.md` as the entry point and
`docs/predictive-slicer/IMPLEMENTATION_PLAN.md` as the canonical milestone plan.
Keep rationale and open gates in the existing area documents; link them from
code when an isolated edit could invalidate another contract. Copies outside
this Git root are historical context, not the current plan.

When available, select skills using the guidance in `CODEX_START_HERE.md`.
Skills support the task; they do not add milestone authority or require a
standing review ceremony. Tests and physical evidence retain their distinct
meanings even when a memory audit reports no findings.

## Verification

Run the independent smoke build described in `CODEX_START_HERE.md`. When the full
preFlight dependency toolchain is available, also build the affected production
targets and run relevant upstream tests.
