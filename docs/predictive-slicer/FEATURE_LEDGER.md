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
| Independent verifier | Designed | Separate implementation and mutation suite |
| Legacy preFlight adapter | M1 source inventory and regression infrastructure implemented | Native build and end-to-end golden qualification; see M1_STATUS.md |
| Full GUI build | Not verified | Dependency-complete Windows/Linux builds |

Labels used in this project:

- **Established:** supported by shipping implementation or repeatable literature.
- **Engineering hypothesis:** plausible from known physics but not validated here.
- **Executable seed:** deterministic code proving an interface/invariant, not a
  calibrated production model.
- **Qualified:** passed a declared experiment over a recorded process envelope.
