# Underspecified two-tool offsets: preserved upstream failure

Both archived programs were generated with predictive analysis **disabled** by
the same native production DLL. `evidence.json` records commands, hashes and the
failed four-run comparison; the first two disabled outputs are retained here.
The unchanged comparator rejects them even with the generation timestamp ignored.
The first executable difference is `G1 X4.200 Y0.260 F7200` versus
`G1 X4.400 Y0.260 F7200`. Later wipe-tower moves differ too. The analysis source
reports still matched; accepting that alone would have concealed a real failure.

The AMF assigns two tools and the profile specifies two nozzles, but the default
`extruder_offset` has one entry (confirmed by both configuration footers).
`GCode/WipeTowerIntegration.hpp` copies that vector, and
`post_process_wipe_tower_moves` directly indexes initial/new tool IDs in its
`.cpp`. Tool 1 thus exceeds the provided vector; the unchecked configuration is
not deterministic. This defect exists with analysis disabled. No production
emitter fix, comparator relaxation or motion change is included in M1.

The failing override is retained as `multi_material-underspecified.ini`; it was
used with `tests/predictive/jobs/base.ini` and `multi_material.amf`. The accepted
fixture explicitly specifies `extruder_offset = 0x0,0x0`. Keeping the wipe tower
and both real tool changes, that fully specified profile passed two complete
four-run suites with the same binary. Underspecified multi-tool profiles remain
an upstream limitation, not a qualified configuration. Reproducing undefined
behavior is not guaranteed to give the same coordinates on another process/run.
