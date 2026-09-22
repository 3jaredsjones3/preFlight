# preFlight integration map

Baseline: preFlight v1.2.0, commit `92481fc5d2b60ba46abebe4d644284f037c67872`.

## Existing stages to preserve

- `PerimeterGenerator.cpp` produces classic, Arachne, Athena, Serpentine, and
  interlocking extrusion entities.
- `Layer.hpp` owns region and support extrusion collections.
- `ExtrusionEntity.hpp` and `ExtrusionEntityCollection.hpp` are the common legacy
  traversal surface.
- `GCodeGenerator::process_layer` in `GCode.cpp` converts selected layer entities
  into ordered machine instructions.
- `GCodeGenerator::process_layers` owns layer-level orchestration and postprocess
  stages such as spiral vase, pressure equalization, cooling, and find/replace.
- `GCodeObject` and the preprocessor layer provide a structured representation
  useful after G-code generation, but are too late for the primary bead compiler.

## M1 insertion point

The first adapter should run after conventional geometry generators have produced
`ExtrusionEntity` trees but before `process_layer` irreversibly lowers them to
textual G-code. It must initially be analysis-only.

Recommended modules:

```text
src/libslic3r/Predictive/LegacyExtrusionAdapter.{hpp,cpp}
src/libslic3r/Predictive/LegacyRoleMapping.{hpp,cpp}
src/libslic3r/Predictive/ArtifactJson.{hpp,cpp}
src/libslic3r/Predictive/CanonicalGCodeComparator.{hpp,cpp}
```

The adapter must recurse through collections without reordering them and map:

- scaled XY coordinates plus layer print Z;
- variable width at every junction;
- flow-derived height and volumetric deposition;
- extrusion role, bridge status, overhang status, and tool/material;
- object, instance, layer, region, island, and cancel-object identity;
- closed/open topology and source ordering;
- seam and wipe behavior where represented.

## Identity mode

M1 does not optimize. It performs:

```text
legacy entity tree -> BeadGraphIR -> analysis report
                   \-> untouched legacy emission
```

Only after equivalence is proven should an IR-to-legacy or direct machine-program
lowering replace the untouched branch. This prevents the adapter milestone from
quietly changing print behavior.

## Postprocessors

Pressure equalization and cooling currently operate after layer G-code creation.
J-Slice must either model their final effects or move equivalent behavior into the
timed program before claiming that ArtifactIR predicts emitted output. Until then,
analysis reports must state which downstream transformations were not modeled.

## Build integration

The reference Predictive sources are listed in `src/libslic3r/CMakeLists.txt`.
The independent research target under `research/predictive_core` deliberately has
no GUI or geometry-kernel dependencies and is the fast invariant test surface.

## Compatibility policy

- Preserve `oozebot/preFlight` provenance and upstream history. Remote aliases
  are checkout configuration, not project identity: inspect `git remote -v`
  and branch tracking before publishing. This checkout's `upstream` points to
  `3jaredsjones3/preFlight`, the user-authorized publication target; do not
  repoint it based on its name.
- Preserve AGPL notices and upstream history.
- Keep J-Slice work in reviewable milestone commits.
- Rebase only with golden adapter tests available.
- Treat changes to legacy `ExtrusionEntity` semantics as schema migrations.
