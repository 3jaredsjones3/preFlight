# J-Slice

**Predictive 3D print generation engine**

J-Slice predicts the part a printer will produce, then compiles and optimizes the
manufacturing process required to make that predicted artifact converge on the
intended design.

This repository begins as a research fork of preFlight v1.2.0. Existing slicing,
Athena/Arachne walls, Serpentine paths, interlocking, profiles, GUI, and export
infrastructure remain upstream foundations. J-Slice adds an intermediate
representation for 3D bead paths, manufacturing intent, temporary structures,
machine fingerprints, timed programs, forward prediction, uncertainty, and
independent verification.

Current status: executable M0/M0.1 reference core. The next milestone is a
lossless, analysis-only adapter from preFlight extrusion entities.

Start with [`CODEX_START_HERE.md`](CODEX_START_HERE.md).
