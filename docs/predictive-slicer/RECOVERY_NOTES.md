# Reconstruction provenance

The original experimental branch was created in a transient workspace and had
two local commits:

- `c85fdf5 Add predictive slicer compiler M0`
- `7205ad3 Plan and seed residual support compiler`

The branch, patch attachments, and Git objects were removed by automated
workspace maintenance before they were stored persistently. The surviving
conversation export retained the architecture, deliverable names, milestones,
verification claims, and public summaries, but not the full patch payloads.

This repository was therefore reconstructed on the same upstream preFlight
commit. It preserves the agreed architecture and implements the same categories
of executable seeds, but it is not byte-identical to the lost branch. New work
must cite this repository's own commit IDs rather than the historical hashes.

The reconstruction was accepted only after:

- strict C++20 compilation with warnings as errors;
- smoke checks for valid compilation, invalid path/contact graphs, support
  drawability, experimental primitive gating, and bounded live policy;
- ASan/UBSan execution;
- JSON Schema validation;
- Git whitespace/diff checks.
