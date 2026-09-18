# M1 regression infrastructure

`golden/` contains seven reviewed **synthetic entity snapshot** outputs. They test
the dependency-free adapter boundary and JSON format. They are not G-code captured
from a production slicer and do not qualify M1's end-to-end exit criterion.

Run the isolated CMake/CTest suite from `CODEX_START_HERE.md`. It checks every raw
point and flow attribute, traversal and parent identities, width discontinuities,
volume accounting, unknown metadata, JSON determinism/escaping, all role bits,
and rejection of G-code mutations. The Python checks replay the geometry/volume
mapping independently. JSON Schema validation runs when `jsonschema` is installed.

Golden updates are explicit, never part of the normal test run:

```powershell
build/predictive-core/Release/predictive_adapter_tests.exe tests/predictive/golden --update
git diff -- tests/predictive/golden
```

The native test uses **actual** `ExtrusionEntityCollection`, `ExtrusionLoop`,
`ExtrusionMultiPath`, and `ExtrusionPathOriented` objects. Enable it in a full
preFlight build with `-DJS_SLICE_NATIVE_ADAPTER_TESTS=ON`, build target
`predictive_native_adapter_tests`, and run that executable. It deliberately is
not replaced with mock upstream classes in the isolated build.

## Full slicing qualification

`jobs/manifest.json` lists seven small AMF jobs with pinned profile overrides and
feature expectations: mechanical, thin-wall/Arachne, bridge/support, multi-object
cancel labels, Serpentine, interlocking/Athena, and two-material geometry with a
wipe tower. Geometry/profiles are inputs awaiting qualification, not accepted
baseline outputs. Upstream no longer exposes a separate classic generator choice;
the entity fixtures exercise its common path/loop representation.

Regenerate input geometry explicitly with `python tools/predictive_fixture_jobs.py`.
Run the suite with dependency-complete binaries:

```powershell
python tools/predictive_golden.py run --manifest tests/predictive/jobs/manifest.json --baseline C:/build/baseline/preFlight-console.exe --candidate C:/build/candidate/preFlight-console.exe --comparator build/predictive-core/Release/predictive_compare_gcode.exe --output build/m1-qualification
```

The output directory must not already exist. Each job runs upstream ordinary
export, candidate ordinary export, candidate analysis export, and repeated
analysis. All G-code is compared against upstream; the two reports must be byte
identical. Feature expectations reject jobs that did not generate the intended
role/tool/object. Logs, input/binary hashes, commands and comparison evidence are
saved. No golden output is approved automatically. Profile/geometry changes must
be reviewed and rerun; save qualified baseline outputs with the evidence record.

The comparator uses exact bytes first, then a conservative canonical comparison:
it normalizes finite numeric words for a small explicit command set, including
compact G-code, preserving command order and all parameters. Unknown commands,
macros, duplicate-word lines, checksummed lines and comments remain opaque. It
does not equate alternate modal programs, resampled moves, or arbitrary firmware
syntax. False negatives are preferable to hiding a regression. This is not a
safety verifier.

No comments are ignored by default. If the first full run establishes a truly
volatile full-line comment, add its narrow prefix to the manifest after reviewing
the diff. Never exclude object labels, roles, configuration, or machine commands.
Numeric command equality uses parsed IEEE doubles with no tolerance.
