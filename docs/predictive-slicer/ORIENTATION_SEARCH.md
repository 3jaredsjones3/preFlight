# Intent-aware orientation search

## Why orientation is a compiler problem

Orientation changes every downstream quantity: bead directions relative to load,
datum stair-stepping, supports, scars, thermal history, warpage, travel, time,
tool accessibility, and failure consequences. Minimizing support volume alone is
therefore the wrong objective.

## Inputs

- Exact geometry and semantic feature identity.
- User-declared datums, finish zones, loads, contacts, and forbidden anchors.
- Machine fingerprint and material process envelope.
- Confidence-tagged model/vision suggestions approved by the user.

## Search

1. Generate coarse candidates from stable bed faces, datum normals, symmetry,
   load axes, and sampled SO(3) orientations.
2. Reject candidates violating build volume or mandatory accessibility.
3. Run reduced-fidelity compilation for each candidate.
4. Refine nondominated neighborhoods.
5. Return a Pareto set with explanations, never a falsely universal optimum.

## Objectives

- predicted structural utilization and weak-plane exposure;
- functional datum error and uncertainty;
- visible surface finish and seam placement;
- residual support mass, scars, and removal access;
- thermal distortion/warpage risk;
- print time, material, tool changes, and failure exposure.

## Comparability

Every candidate report records model fidelity, compiler build, fingerprint,
settings, uncertainty, and which constraints were approximated. Candidates are
not compared across inconsistent models without an explicit warning.

## User interaction

The system may suggest “bracket-like load here” or “likely mating face,” but the
user confirms structural intent. Numeric loads are never silently assigned by a
language or vision model.
