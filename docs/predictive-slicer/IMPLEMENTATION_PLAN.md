# J-Slice implementation plan

## Release rule

Each milestone must have an executable completion criterion. Documentation or
interfaces without tests do not complete a milestone. Printer-changing features
remain disabled by default until their calibration and verification gates pass.

## M0: predictive compiler seed

Status: implemented as an isolated C++20 core.

- Geometry fields, functional datums, bead graph, timed moves, ArtifactIR.
- Machine fingerprint and process limits.
- Deterministic planning/simulation seeds.
- Provenance-preserving, certification-gated reference emitter.
- Strict warning build and smoke test.

Completion evidence: `tools/predictive_core_smoke.cpp`.

## M0.1: temporary-structure semantics

Status: implemented at graph/contract level.

- Contact contracts with minimum and maximum bond utilization.
- Differentiated support duties.
- Support nodes, members, loads, lifetimes, accessibility, and scar budgets.
- Deterministic graph drawability validation.
- Conventional member lowering.
- Explicit rejection of unqualified free-space strands and arches.

## M1: lossless preFlight adapter

Status: production-build and end-to-end equivalence exit criterion passed for
the seven pinned fixtures on native Windows. Native capture, source-order
analysis and opt-in CLI sidecars remain analysis-only; downstream emitter state
and unavailable classic generation remain explicit limits. See
[M1_STATUS.md](M1_STATUS.md) for commands, evidence and uncovered branches.

1. Define stable adapters from `ExtrusionEntity`/collections into `BeadGraphIR`.
2. Preserve object, instance, layer, region, tool, material, extrusion role,
   width, height, flow, speed, seam, bridge, and cancel-object identity.
3. Cover Athena, Arachne, Serpentine, classic perimeters, infill, support, skirt,
   brim, wipe, and multi-material paths.
4. Add an identity lowering path that leaves ordinary output unchanged.
5. Add a headless analysis mode that emits deterministic ArtifactIR JSON.
6. Establish golden fixtures for mechanical, thin-wall, bridge/support,
   multi-object, Serpentine, interlocking, and multi-material jobs.

Exit criterion: analysis mode traverses the new IR while baseline output is
byte-identical where deterministic metadata permits, otherwise semantically
identical under a canonical G-code comparator.

## M1.5: independent trusted verifier

Status: complete for the final-G-code contract/integrity boundary. Seven accepted
M1 goldens, twenty focused production mutations, packet/path/datum mutations,
independent modal replay and actual off/on comparisons pass under explicit
synthetic machine/startup assumptions. See [M1_5_STATUS.md](M1_5_STATUS.md) and
[TRUSTED_VERIFIER.md](TRUSTED_VERIFIER.md). Exact surface geometry, deposited-part
collision, dependency/contact physics and authenticated calibration/artifact
proofs remain deferred and reject when requested.

- Separate library/executable with no generator or repair APIs.
- Parse final G-code independently.
- Check syntax/modal state, build volume, axis/flow limits, extrusion continuity,
  object boundaries, tool envelope, datum restrictions, fingerprint binding and
  canonical work-packet content.
- Independently check sidecar path ranges/digests, object/tool identity,
  predecessor order and temporary lifetimes.
- Emit machine-readable findings and fail closed on unknown commands required
  for safety reasoning.

Exit criterion: representable invariants reject focused mutations while accepted
baseline jobs remain accepted; unsupported physical proofs are explicitly
versioned, reported unproven and assigned to later calibration/geometry work.

## M2: thermal-debt scheduler

- Fit simple material cooling and bonding models from coupons.
- Construct a local thermal-neighbor graph from BeadGraphIR.
- Reorder only dependency-equivalent paths.
- Optimize travel plus contact-age/temperature debt.
- Report both expected benefit and uncertainty.
- A/B test against unchanged preFlight ordering on AD5M and AD5X.

Exit criterion: repeated coupon tests show a preregistered improvement or the
feature is rejected/revised. Print time and visible quality are recorded as
guardrails.

## M3: machine fingerprint and evidence loop

- Calibration suite for flow, pressure response, dimensional bias, corners,
  thermal step response, resonance, and tool envelope.
- Versioned fingerprint hashing and compatibility checks.
- Coupon generator linked to the same fields and settings as the part.
- Predicted-versus-measured record format and parameter fitting.

## M4: geometric pre-compensation

- Hole, corner, elephant-foot, bead-spread, and shrinkage models.
- Solve bounded inverse corrections per feature.
- Respect datum permissions and uncertainty limits.
- Never stack compensation mechanisms without an explicit composition model.

## M4.5: intent-aware orientation search

- Generate coarse candidates, then reduced-fidelity compilations.
- Return a Pareto set across strength, finish, support, tolerance, warpage,
  removal access, time, and uncertainty.
- Require user confirmation for inferred loads or mating intent.

## M5: field-based geometry

- Production height/direction/density field solvers.
- Slightly curved layers and safe nonplanar skins.
- Stress-aligned reinforcement with confidence-aware fallback.
- Composable interlock and variable-width constraints.

## M5.5: residual support compiler

- Compute unsupported residual after orientation, warping, lateral deposition,
  and bridges.
- Optimize conventional temporary topology against loads and removal contracts.
- Lower accepted topology to ordinary bead paths for existing scheduling and
  simulation.

## M6: closed-loop observation

- Companion-computer capture and object registration.
- Detection, confidence calibration, and bounded policy layer.
- Start with observe/report, then pause/exclude-object.
- Flow or speed corrections require dedicated experiments and verifier limits.

## M6.5 research: drawn support structures

Disabled until experiments qualify:

1. Vertical/inclined strand draw ratio, diameter, breakage, sag, and position.
2. Fresh-strand transit joints to cooled masts.
3. Downward extrusion over arch apices.
4. Frame-node strength versus removable final interfaces.
5. Nozzle contact/drag loads and member buckling.

Passing isolated tests does not authorize general generation. The compiler also
needs collision-free construction and removal-access proofs.

## M7: product integration

- J-Slice Studio controls, explanations, reports, and profile migration.
- Incremental compilation keyed by semantic dependencies.
- Reproducible work packets: source hash, settings, fingerprint, compiler build,
  fields, ArtifactIR, verifier result, and emitted program.
- Signed release builds and public experimental labels.
