# AD5M/AD5X thermal scheduling coupon protocol

This protocol defines evidence collection; it is not a claim that a coupon has
been printed. Register printer serial, firmware, nozzle, material lot, bed and
ambient conditions before printing. Use the same commit, machine fingerprint,
process settings and fixture geometry for both variants.

For each printer/material pair, print matched coupons with preFlight source order
and with an analysis-only proposal copied into the experimental order. Keep the
proposal sidecar and final G-code immutable. Record contact-age temperature
observations at the selected neighbor interface, destructive bond-proxy load,
visible surface/bridge/support quality, print time, failures and all exclusions.
Do not pool results across printers or material lots.

Raw observations are CSV with one row per coupon/interface and are normalized
into thermal-calibration.schema.json. A fit is accepted only when metadata is
complete, ages span the fit range, measurements show cooling, and residual and
uncertainty diagnostics are retained. The fit artifact is bound to the source
dataset hash and machine fingerprint. Physical A/B evidence must be preregistered
before a recommendation is enabled.

## Software handoff

Use the isolated package from the repository root:

```powershell
python tools/m2_coupon.py templates --out build/m2-coupon-input-templates
python tools/m2_coupon.py prepare --spec EXPERIMENT.json --gcode SOURCE.gcode `
  --machine AD5M.machine.json --manifest SOURCE.manifest.json `
  --packet SOURCE.work-packet.json --proposal M2-PROPOSAL.json `
  --model MEASURED-MODEL-FIT.json --verifier build/m2-trusted/jslice_verify.exe `
  --out build/m2-evidence/AD5M/EXPERIMENT-ID
```

`prepare` requires a real confirmed printer identity, real machine fingerprint,
measured non-synthetic model, matching calibration hash, complete path
contracts, and a recommendation-eligible analysis-only proposal. It consumes
the proposal's baseline/proposed order; it never invents an optimization. The
trusted verifier is run independently on both generated programs. The pair
comparison then independently checks equal path IDs, command geometry and
process contracts, volume, tool/material assignment and dependency order. Only
path ordering, resulting travel and resulting timing may differ.

The bundle layout is:

```text
experiment-001/
  baseline/{program.gcode,manifest.json,work-packet.json}
  proposed/{program.gcode,manifest.json,work-packet.json}
  evidence/{pair-integrity.json,input-hashes.json,verifier-reports.json}
  experiment.json machine.json model.json proposal.json manifest.json
  print-order.csv raw-observations.csv normalized-measurements.template.json
  operator-checklist.md README.md
```

The generated print-order sheet is randomized from the recorded seed and must
be followed. Do not fit the thermal model using confirmatory IDs. After the
physical run, preserve the raw CSV and instrument records, normalize them, fit
only the registered calibration IDs, and evaluate only confirmatory IDs:

```powershell
python tools/m2_coupon.py fit --measurements CALIBRATION.json --output MODEL-FIT.json
python tools/m2_coupon.py evaluate --bundle build/m2-evidence/AD5M/EXPERIMENT-ID `
  --measurements CONFIRMATION.json --output EVALUATION.json
```

An evaluation can pass only if every preregistered inclusion/guardrail rule is
satisfied, the included proposed bond-proxy mean clears the registered minimum
improvement, and no required observation is missing. Exclusions remain in the
record with reasons. A failed rule means M2 failed/revise; incomplete data is
not a pass. AD5M and AD5X results are separate and are never pooled.

Before Jared can run a printable bundle, he must provide the exact AD5M and
AD5X serials and firmware/startup assumptions, machine limits/tool envelope and
fingerprint hashes, nozzle and material-lot records, measured calibration data
and fit hash, proposal path contracts, camera/thermography calibration, fixture
identity, and the preregistered acceptance thresholds. This checkout contains
none of those real physical inputs; synthetic fixtures are test-only.

## M2.1 machine-evidence handoff

Use `M2_1_MACHINE_EVIDENCE.md` and the generated worksheets before preparing a
coupon bundle. The bootstrap records are provenance ledgers, not printer
profiles: manufacturer ratings are not calibrated operating limits, firmware
control ceilings are not physical ratings, and Orca start G-code is not proof
of the Flash Studio/firmware startup sequence. AD5M centered coordinates and
AD5X corner-origin coordinates must remain explicit.

For each printer, Jared must next capture the nameplate serial and exact
firmware; nozzle identifier/type/diameter; slicer/profile identity and hash;
verbatim profile start program and hash; exported start/end sequence artifacts
and hashes; capability-applicable tool-change evidence; actual G-code coordinate convention; material
manufacturer/type/color/lot; ambient conditions; operator/date; and the full
nozzle-axis toolhead, cable/PTFE, clearance, bed-edge, and obstacle worksheet.
AD5X additionally requires cutter, wiper/purge, and filament-handling obstacle
measurements. Photographs must retain scale/fiducial metadata. No inferred or
image-generated mesh satisfies exact collision proof.

AD5M tool change is `not_applicable` because the machine is single-material and
has no ordinary tool-change sequence; no empty or N/A artifact may be invented
or hashed. AD5X tool change is `required` and must identify a real captured
multi-material sequence with a lowercase 64-character SHA-256. The same
`required` versus `not_applicable` structure applies to any future sequence that
machine capabilities may legitimately omit.
