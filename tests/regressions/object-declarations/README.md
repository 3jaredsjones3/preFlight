# Pre-existing OctoPrint declaration nondeterminism

Discovered during M1 production qualification. `disabled-a.gcode.gz` and
`disabled-b.gcode.gz` preserve two actual `--export-gcode` outputs with predictive
analysis **disabled**. They have identical executable bytes and in-body object
boundaries, but different independent header-comment and `objects_info` array
ordering. `disabled.diff` is the complete text diff. `evidence.json` records the
commands, source hashes, output hashes and pre-fix slicer DLL hash.

`LabelObjects::init` groups objects in a map keyed by pointer address. That map
order leaked into header presentation. The focused fix preserves all existing
label strings and assigned IDs, then stable-sorts **OctoPrint only** by model
object/instance indices. Source order breaks ties. Lookup during emission remains
by `PrintInstance`; the toolpath and its boundaries are untouched. Firmware
declarations may carry semantics, so their order and ID assignment are unchanged.
This fix does not qualify firmware-label determinism or expand the M1 adapter.

The native regression controls allocation-vs-model order, checks two objects and
two instances each, checks in-body identities, and confirms unchanged firmware
declarations. `native-before.txt` records the reproduced failure before the fix;
`native-after.txt` records the pass afterward. Build with
`-DJS_SLICE_NATIVE_ADAPTER_TESTS=ON`, then run:

```powershell
build/m1-native/src/libslic3r/legacy_label_objects_tests.exe
python tools/legacy_label_gcode_regression.py --slicer build/m1-native/src/Release/preFlight-console.exe --comparator build/m1-native/research/predictive_core/predictive_compare_gcode.exe --output build/m1-label-qualification
```

The output directory must be new. The production regression exports five ordinary
and five analysis-enabled jobs. All ten pass the unchanged canonical comparator;
all five reports are byte-identical. A separate, narrowly scoped historical patch
audit proves the only pre/post-fix difference is independent header ordering:
every executable byte, in-body object boundary, label identity, outline and other
metadata is preserved (apart from the explicitly identified generation timestamp).
That audit is **not** used for off/on comparison. The comparator still rejects the
archived disabled/disabled pair. `qualification.json` preserves the ten-run result.
