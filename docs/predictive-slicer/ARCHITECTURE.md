# J-Slice predictive compiler architecture

## Thesis

J-Slice treats additive manufacturing as an inverse problem. The requested
geometry is not assumed to be the geometry a machine will produce. The compiler
constructs a candidate deposition program, predicts its physical result, and
changes the program until the predicted artifact satisfies declared contracts.
G-code is a lowering target, not the primary design object.

The central invariant is:

> No optimization may silently weaken a functional, safety, contact, or machine
> contract. Uncertainty is part of the result and can prevent emission.

## Representation stack

J-Slice does not force every problem into a voxel grid. Each stage keeps the
representation appropriate to its mathematics and exchanges explicit contracts.

1. **Source geometry and identity**
   - Meshes initially; STEP/B-Rep identity is a later input path.
   - Object, region, surface, and semantic feature identities are retained.
2. **Manufacturing intent**
   - Functional datums, tolerance envelopes, finish regions, loads, material,
     contact intent, support-removal constraints, and user-approved priors.
3. **GeometryFieldIR**
   - Height/layer coordinate, preferred direction, density, bead width, and
     confidence sampled over the relevant volume.
   - Exact protected regions remain exact geometry contracts rather than being
     reduced to field samples.
4. **SupportGraphIR**
   - Temporary structural topology: anchors, contacts, members, duties, loads,
     lifetimes, fracture interfaces, scar budgets, and print dependencies.
5. **BeadGraphIR**
   - Three-dimensional bead paths with width, height, speed, material, role,
     dependencies, contact contracts, and provenance.
6. **Timed machine program**
   - The scheduled graph lowered to time-parameterized travel and deposition
     moves under machine and extrusion limits.
7. **ArtifactIR**
   - Predicted deposited matter, temperature history, bond utilization,
     geometric error, datum envelopes, uncertainty, and certification findings.
8. **Machine output**
   - G-code or another controller format emitted only after the selected
     certification policy permits it.

## Core contracts

### Functional datums

A datum protects geometry such as a bore, mating plane, thread, bearing seat, or
cosmetic face. It carries positional and surface tolerances plus explicit
permissions for nonplanar warping, interlocking, and dynamic compensation.

### Contact contracts

Contacts express intent rather than merely adjacency:

- `StructuralBond`: a permanent joint with a minimum bond requirement.
- `TemporaryBrace`: strong enough during construction but removable afterward.
- `DesignedFracture`: bond utilization must remain between lower and upper
  limits so the interface survives printing and fails predictably during removal.
- `NonBondingContact`: geometric support without a polymer weld.

The upper bond limit is essential. Permanent structures maximize useful bonding;
temporary structures optimize inside a bounded process window.

### Machine fingerprint

Every predictive result is tied to a versioned fingerprint containing build
volume, axis limits, flow limits, pressure response, tool envelope, and measured
resonance modes. A production fingerprint will also carry calibration provenance,
fit uncertainty, material/nozzle compatibility, and validity intervals.

### Provenance

Every generated path names its source object and feature, generating pass,
optional source layer, and parent path. A report must be able to explain why a
path exists and which inputs invalidate it.

## Compiler passes

The M0 reference core contains deterministic seeds for:

- bead/dependency/contact validation;
- stress-to-direction and stress-to-density mapping;
- resonance-notched spectral interlock deformation;
- slope- and datum-constrained nonplanar projection;
- precedence, travel, and thermal-priority scheduling;
- kinematic/volumetric time parameterization;
- modal vibration prediction;
- bounded geometric pre-compensation;
- nozzle-pressure release planning;
- build-volume and swept-envelope certification hooks;
- sparse thermal/bond forward simulation;
- bounded live-response policy decisions;
- certification-gated G-code emission.

These implementations establish interfaces and invariants. They are not yet
calibrated physical models and must not be described as production predictors.

## Support compilation

Support is an optimization residual. The intended ordering is:

1. Search part orientation.
2. Use safe layer-field warping.
3. Use qualified lateral/nonplanar deposition.
4. Use qualified bridges.
5. Generate designed temporary structures.
6. Fall back to conventional support where necessary.

`SupportDuty` separates sag arrest, deposition reaction, thermal anchoring,
dynamic bracing, stability bracing, and release interfaces. Free-space strands
and arches remain gated until experiments establish draw, joint, load, lifetime,
and fracture envelopes.

## Scheduling and thermal debt

Scheduling is constrained optimization over a dependency graph. The first
printer-facing optimization will reorder otherwise equivalent islands and
features to control the time and temperature at which neighboring beads meet.
It must preserve object exclusion boundaries, material/tool changes, bridges,
seams, cooling constraints, support lifetimes, and functional datums.

The M0 scheduler only supplies deterministic precedence/travel/thermal-priority
behavior. M2 replaces its heuristic thermal term with a calibrated sparse model
and measurable bonding-window objective.

## Trust boundary

The compiler may generate, optimize, and repair candidate paths. The trusted
verifier may only parse and reject a final machine program. It must be a smaller
target with independent geometry and machine checks. It never repairs output and
does not share mutable compiler state.

## Live control boundary

Vision models propose observations and priors; they do not silently assign loads,
alter density fields, or invent corrections. Live policies are confidence-gated,
magnitude-bounded, reversible when possible, and logged. Object exclusion and
pause are preferred to physical part ejection.

## Current implementation boundary

M0/M0.1 compile and run as an isolated C++20 research target. They are not yet
connected end-to-end to preFlight's `ExtrusionEntity` pipeline. The next milestone
is a lossless legacy adapter and analysis-only artifact report. Until that passes
golden regression tests, J-Slice is architecture and executable research code,
not a replacement slicer.
