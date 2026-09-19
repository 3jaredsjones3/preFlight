# Feature and evidence ledger

| Capability | Current state | Evidence needed before printer-changing default |
|---|---|---|
| 3D bead/dependency IR | Executable M0 seed | Lossless M1 adapter and golden jobs |
| Functional datum contracts | Executable M0 seed | UI/3MF persistence and verifier enforcement |
| Stress-to-field mapping | Executable nearest-sample seed | FEA import, confidence propagation, mechanical coupons |
| Spectral interlock | Executable bounded seed | Strength tests and machine-spectrum validation |
| Slight nonplanar projection | Executable bounded seed | Nozzle/tool collision model and surface tests |
| Thermal-aware scheduling | Heuristic seed | Calibrated temperature/bond model and A/B coupons |
| Modal vibration prediction | Reduced model seed | Fingerprint measurement and displacement validation |
| Geometric pre-compensation | Bounded hook | Per-feature calibration and metrology |
| Pressure release | Reduced model seed | Extrusion-pressure calibration |
| Swept collision checking | Build-volume hook only | Independent full tool/part swept-volume verifier |
| Sparse thermal/bond model | Executable reduced model | Material/nozzle/cooling calibration and uncertainty fit |
| Closed-loop decisions | Bounded policy seed | Calibrated detector and controller integration |
| Contact contracts | Executable M0.1 | Measured permanent/fracture process windows |
| Conventional support graph | Executable M0.1 | Buckling/load/removal models |
| Drawn strands and arches | Rejected unless qualified | Strand, joint, arch, load, lifetime, collision experiments |
| Orientation Pareto search | Designed | Reduced compiler and stable objectives |
| Independent verifier | M1.5 contract/integrity boundary complete: separate four-input library/CLI, canonical work packets, 7 production goldens, 20 focused mutations, path precedence/lifetime and conservative datum cases qualified under declared synthetic inputs | Exact surface/part collision, dependency/contact physics, authenticated calibration/artifact binding, more dialects and signatures remain deferred; see M1_5_STATUS.md |
| Legacy preFlight adapter | M1 native source inventory and 7-job off/on production equivalence qualified | Downstream emitter state remains outside the source report; see M1_STATUS.md |
| Full GUI build | Not verified | Dependency-complete Windows/Linux builds |

Labels used in this project:

- **Established:** supported by shipping implementation or repeatable literature.
- **Engineering hypothesis:** plausible from known physics but not validated here.
- **Executable seed:** deterministic code proving an interface/invariant, not a
  calibrated production model.
- **Qualified:** passed a declared experiment over a recorded process envelope.
