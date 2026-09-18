# Research position

## What J-Slice is attempting

Mainstream slicers primarily transform geometry into an open-loop instruction
stream. J-Slice adds a representation of the expected manufactured artifact and
uses that representation to coordinate geometry, thermal history, machine
dynamics, support, tolerances, and evidence.

The novel value is not any single toolpath technique. Brick staggering,
nonplanar layers, variable width, stress-aligned infill, input shaping, and
support optimization each have prior art. The project contribution is a
composable compiler boundary in which these passes declare what they change,
what they protect, and how their effects are predicted and verified together.

## Positions taken

1. **Fields are planning variables, not the only data structure.** Exact boundary
   geometry, sparse thermal state, support topology, timed motion, and machine
   dynamics retain distinct representations.
2. **Machine-specific calibration is mandatory.** A prediction without a bound
   fingerprint is an untracked assumption.
3. **Uncertainty can block output.** Unsupported confidence is not converted into
   a precise-looking number.
4. **Language/vision models label intent and propose priors.** They do not silently
   set structural loads or density fields.
5. **Resonance is constrained, not surfed.** Toolpaths may be parameterized to
   reduce spectral excitation. Deliberately relying on coherent frame rebound is
   too sensitive to position, damping, temperature, and disturbances.
6. **Supports are temporary structures.** Their loads, lifetime, contact strength,
   removal access, and scars are explicit design quantities.
7. **Coupons accompany claims.** A predicted benefit without a matched experiment
   remains a hypothesis.

## Relevant research lineage

- CurviSlicer: slightly curved layers on three-axis machines.
- S3-Slicer: generalized multi-axis slicing.
- AtomSlicer: field-aligned nonplanar slicing with continuous paths.
- Arachne/Athena: variable-width perimeter planning.
- Serpentine and interlocking work already present in preFlight.

J-Slice should import established techniques through adapters and contracts rather
than obscure their lineage or rewrite them merely to own the code.

## Claims intentionally not made

- The current thermal model predicts absolute weld strength.
- The current vibration model replaces input shaping or measured calibration.
- A drawable support graph proves collision-free, load-safe construction.
- A vision model can infer reliable numeric service loads from appearance.
- The M0 emitter is a production printer backend.
- Passing the smoke test validates physical performance.
