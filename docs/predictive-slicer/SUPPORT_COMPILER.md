# Residual support compiler

## Objective

Generate the smallest removable temporary structure that makes the otherwise
optimized build program feasible. Support is computed after orientation,
permitted field deformation, lateral deposition, and bridging.

## Inputs

- Candidate part orientation and field solution.
- Residual unsupported deposition regions.
- Predicted nozzle reaction/contact loads and part stability loads.
- Material/process envelope and temperature history.
- Tool swept volume.
- Permitted anchors, forbidden/scar-sensitive surfaces, and removal corridors.
- Contact contracts and uncertainty limits.

## Support graph

Nodes:

- bed anchors;
- explicitly permitted part anchors;
- support contacts;
- seed props;
- transit joints.

Members:

- conventional stacked struts;
- qualified bridges;
- interface strands;
- experimental axial drawn strands;
- experimental arch candidates.

Member contracts include service load, lifetime, unsupported span, diameter,
duty, dependencies, scar budget, removal access, and provenance.

## Duties

- **Sag arrest:** limit deformation of a newly deposited span.
- **Deposition reaction:** resist forces imposed by the moving nozzle/extrudate.
- **Thermal anchor:** control warpage or heat-driven movement.
- **Dynamic brace:** limit transient vibration during later deposition.
- **Stability brace:** prevent whole-part tipping or rocking.
- **Release interface:** transfer construction loads through a deliberately
  removable joint.

## Optimization

Hard constraints:

- graph drawability and dependency order;
- qualified primitive/process envelope;
- tool and part swept-volume clearance;
- member stress, buckling, joint strength, and lifetime;
- contact lower/upper bond utilization;
- datum, scar, and removal-access contracts.

Soft objectives:

- support material and added time;
- predicted removal effort and scarring;
- number of free tips and isolated towers;
- uncertainty and sensitivity to calibration;
- preference for two-ended spans where qualified.

## Staged implementation

1. Residual map using existing bridge/overhang classification.
2. Conventional tree/strut graph encoded in `SupportGraphIR`.
3. Load/lifetime checks using conservative bounds.
4. Contact-window scheduling and fracture-interface coupons.
5. Removal-access verification.
6. Experimental drawn primitives only after process qualification.

## Current code boundary

M0.1 validates graph identity, contracts, dependencies, seeding, and maximum span.
It lowers conventional members into bead paths. It deliberately rejects axial
free-space strands and arches because no qualified process envelope is supplied.
This is graph-level drawability, not full collision or structural certification.
