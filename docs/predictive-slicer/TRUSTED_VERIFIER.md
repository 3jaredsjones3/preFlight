# Independent trusted verifier

## Purpose

The verifier answers one narrow question: is this final machine program allowed
to execute under this machine fingerprint and manufacturing contract? It does not
generate paths, optimize them, or repair failures.

M1.5 implements the independent linear-program and contract/integrity checks
below. Its `accepted` result means **conditional static acceptance under the
supplied fingerprint, work packet and startup assumptions**, not authorization
to run a printer. Exact deposited-part collision, support/contact physics,
calibration authentication and firmware behavior absent from final G-code are
explicitly deferred; requesting them rejects. See `M1_5_STATUS.md`.

## Separation rules

- Separate target and namespace from the predictive compiler.
- No dependency on generator passes or mutable compiler state.
- Parse the emitted program independently.
- Unknown state-changing commands fail closed unless explicitly modeled.
- Findings are deterministic, machine-readable, and tied to source offsets.

## Required checks

1. Syntax, modal state, units, coordinate mode, extrusion mode, and tool state.
2. Build-volume and temperature bounds.
3. Axis speed/acceleration and volumetric-flow limits.
4. Extrusion continuity, impossible negative deposition, and unsafe startup/end.
5. Conservative translated tool-envelope/build-volume checks and datum bead
   sweeps when a bound envelope is supplied.
6. Object identity and exclude-object boundary preservation.
7. Protected datum and forbidden-region violations.
8. Required dependencies and unsupported deposition.
9. Contact lower/upper contracts where sufficient evidence exists.
10. Exact binding to fingerprint, compiler manifest, optional intent/artifact
    payloads and the final program through a canonical work packet.
11. Independent path command ranges, digests, object/tool identity, predecessor
    order and temporary-structure lifetimes.
12. Conservative datum bead-sweep envelopes when the packet supplies them.

## Architecture

```text
G-code + fingerprint + verification manifest + work packet
                  |
             strict parser
                  |
        independent state replay
                  |
 geometry / motion / contract checks
                  |
        deterministic JSON report
        (signing is future work)
```

## Testing

- Golden accepted programs from unmodified preFlight.
- One mutation class for every safety invariant.
- Property tests over modal-state transitions.
- Differential position/extrusion replay against a second parser.
- Fuzzing for parser crashes and ambiguous commands.
- Fail-closed tests for truncated files and manifest mismatch.

## Non-goals

The verifier does not prove physical material strength. It verifies only those
contracts supported by the supplied model and evidence, and must report
unverifiable claims rather than converting them into approval.

## M2.1 machine/export binding

The M2 coupon handoff has a separate machine-evidence bootstrap contract in
`schema/machine-evidence.schema.json`. It records source class, exact artifact
identity, pinned revision, observation date, qualification state, and
uncertainty for every retained fact. It preserves conflicting AD5M/AD5X
coordinates and distinguishes published limits from qualified coupon limits.

For a non-fixture physical coupon specification, the coupon tool requires the
actual printer serial, firmware identity/version, slicer/profile identity and
hash, exported start/end/tool-change hashes, material manufacturer/type/color/
lot, ambient observation, operator/date, and actual coordinate convention. An
ambiguous coordinate convention, firmware identity, or startup behavior fails
closed. Manufacturer thermal ratings and firmware `max_temp` values are not
treated as measured limits.

The optional collision manifest is evidence metadata only. It separates fixed
and moving geometry and records the nozzle-tip frame, transform, mesh hash,
uncertainty margin, state, and provenance. Inferred or image-generated meshes
remain unqualified and cannot satisfy exact collision-proof requirements.

## Implemented boundary: v1

`src/TrustedVerifier/` defines the `JSlice::Verification` namespace and the
`jslice_trusted_verifier` static library. `tools/trusted_verify.cpp` builds
`jslice_verify`. Both can be built from `research/trusted_verifier` with no
slicer, GUI, geometry kernel, IR, comparator or optimizer target. The only
dependencies are nlohmann JSON 3.12 from the existing supported dependency
bootstrap and system cryptography (Windows BCrypt; OpenSSL Crypto on other
platforms). Only the native Windows build is qualified here.

The API accepts four immutable byte strings: final G-code, a fingerprint, a
verification manifest and a work packet. The CLI opens inputs read-only, emits
a JSON report on stdout and returns 0 for conditional acceptance, 1 for
rejection, or 2 for CLI/I/O failure.
There is no repair, emission, postprocessing, printer or generator API. The
optional root build flag `JS_SLICE_TRUSTED_VERIFIER` defaults off and adds separate
targets only; ordinary slicing does not call the verifier or change behavior.

The parser and state replay were written independently. No generator parser or
canonical comparator code is called or copied. It requires explicit uppercase
commands and decimal words, accepts compact words, LF/CRLF and semicolon comments,
and rejects duplicate parameters, non-finite numbers, oversized magnitudes,
binary data, exponent notation, unsupported parameters, checksums, numbered
transport lines, parenthesis comments and command subcodes. Uppercase `E` starts
an extrusion word; it is never interpreted as a decimal exponent.

State includes physical XYZ, G92 work-coordinate offsets, shared logical E,
units, absolute/relative XYZ and E modes, active logical tool, shared heater
mapping, heater/bed targets and established lower bounds, fan PWM, feedrate,
speed override/backup, per-tool flow overrides and retraction debt, per-axis
velocity/acceleration limits, and print/travel/retract acceleration settings.
G92 changes logical coordinates without moving the machine or clearing physical
retraction debt. Positive drive motion includes unretraction in temperature and
flow checks. Excess unretraction can be a purge; v1 does not infer bead geometry
or filament presence from E alone.

## Versioned inputs and trust assumptions

- `schema/verifier-machine.schema.json`: `js-machine-1`, dialect
  `marlin-cartesian-1`. This is distinct from the unchanged M0
  `machine-fingerprint.schema.json` (version 0.1).
- `schema/verification-manifest.schema.json`: `js-verification-1`, including
  compiler commit/build identity used by packet binding.
- `schema/verification-work-packet.schema.json`: `js-work-packet-1` and
  `js-canonical-json-1`; it binds exact program bytes, canonical fingerprint and
  manifest content, compiler identity, schema versions, path sidecars,
  temporary lifetimes and datum envelopes.
- `schema/verifier-report.schema.json`: `js-verifier-report-1`.

Input schemas are compiled into the library. Unknown fields, wrong versions,
duplicate JSON keys, invalid types/ranges and inconsistent heater/tool/initial
state dimensions reject. A small schema evaluator implements exactly the
keywords used by those three embedded schemas and rejects unsupported schema
keywords. Tests also validate inputs and every report using an independent
Draft 2020-12 JSON Schema implementation.

The manifest binds the exact final program bytes and fingerprint file bytes
using SHA-256; the work packet additionally binds canonical machine and manifest
content, compiler identity and schema versions. It supplies initial physical XYZ, initial logical E,
tool, heater/bed temperatures and fan state, expected object labels, the terminal
marker, and required property names. It has no generator state. It must be
issued or reviewed by a trusted party **after all G-code postprocessors**. A
hash detects a mismatched file; it does not authenticate a manifest or establish
that a declared machine calibration/start position is true. Reports expose raw
and canonical work-packet hashes. Generation timestamps are included in program
binding;
unlike the M1 comparator, the verifier ignores no bytes for hashing. Optional
`; JS_FINGERPRINT_SHA256=<64 lowercase hex>` metadata must match the manifest.

The dialect contract assumes Cartesian millimetre filament E (not volumetric E),
G0/G1 sharing feed state, G90/G91 clearing the M82/M83 override, no hidden
leveling/coordinate transforms, zero tool offsets, no implicit toolchange moves,
no E reset on toolchange, disabled pressure advance, 100% initial speed/flow,
zero initial retraction debt, initially off heater targets, and firmware axis
limits/acceleration initialized to the fingerprint caps. Initial units and
coordinate modes are unknown until declared in the program. Nonzero tool
offsets reject; a future dialect must independently model compensation and
toolchange motion before admitting those machines.

The temperature proof is conditional on firmware honoring waits and maintaining
the controlled temperature. M104/M140 never establish a higher lower bound;
M109/M190 establish the requested lower bound, and lowering a target reduces it.
Sensor error, heater faults and physical cooling between commands remain
unproven. Feed, axis speed and flow calculations use straight-line nominal
motion duration; acceleration, step timing and pressure transients are unproven.
For pure E moves, the faster of scaled/unscaled feed is used conservatively
because firmware may exclude those moves from feed override.

Firmware mode/override references: [M83](https://marlinfw.org/docs/gcode/M083.html),
[M220](https://marlinfw.org/docs/gcode/M220.html),
[M221](https://marlinfw.org/docs/gcode/M221.html), and
[M109](https://marlinfw.org/docs/gcode/M109.html). These document the selected
dialect; they are not a claim that every Marlin configuration behaves identically.

## Work-packet integrity and path boundary

`js-work-packet-1` is a content-addressed sidecar. The verifier recomputes the
exact program hash and canonical JSON hashes for the machine and manifest, then
requires the packet compiler identity to match the manifest and all schema
versions to match the embedded contracts. Canonical JSON sorts object keys and
preserves array order, so formatting and key-order changes do not change a
component address; semantic changes do. A hash is an integrity binding, not a
signature or proof that the issuer is trusted. Optional intent/artifact hashes
are rejected until their content payloads have a versioned binding.

Each path sidecar names an executable-command range and SHA-256 digest. The
verifier independently checks range order, duplicate IDs, object and tool
identity, object-boundary cuts, predecessor topological order, and temporary
structure creation/last-use lifetimes. It does not infer dependencies or
thermal safety from a path sidecar. Missing sidecars mean path precedence was
not requested; a required sidecar mutation fails closed.

An optional `thermal_schedule_proposal` sidecar is accepted under the same
packet schema. The verifier checks only its versioned path identity permutation
and proposed precedence against executable path ranges. It ignores thermal
scores, temperature estimates and bond claims; those remain compiler-side
analysis and are never treated as trusted physical evidence.

M2 coupon packets may additionally attach an optional `coupon_contract` to each
path. It is an identity/process record for the isolated research package
(geometry digest, deposited volume, width/height, speed, temperature, fan, tool
and material); the verifier schema binds its shape but does not accept the
generator's claim as physical proof. `tools/m2_coupon.py verify-pair`
independently recomputes both final programs and compares those contracts and
path command contents before a bundle is handed to an operator.

Datum entries define axis-aligned protected and permitted envelopes plus a
conservative maximum bead radius. Positive-E linear segments are expanded by
that radius and checked for object identity, envelope containment and optional
planarity. Interlocking, exact feature identity, swept collision with deposited
matter and surface tolerances remain unproven and reject when explicitly
required.

## Supported command envelope

| Commands | Implemented interpretation |
| --- | --- |
| G0/G1 | Explicit linear XYZ/E/F, including stationary E and retract/unretract |
| G20/G21, G90/G91, M82/M83 | Inch/mm and independent coordinate/extrusion modes |
| G92 | Explicit XYZE logical reset with preserved physical state |
| Tn | Existing logical tool, zero offsets and no implicit motion |
| M104/M109, M140/M190 | Heater/bed S targets; R also supported for waits; optional T on hotend commands |
| M106/M107 | Fan index and integer PWM |
| M201/M203/M204 | Bounded per-axis and print/travel/retract settings |
| M220/M221 | Feed save/restore/scaling; per-tool extrusion scaling |
| M900 K0 | Disable pressure advance; nonzero values reject |
| G4, timed M1 | Bounded dwell; indefinite/user-controlled pauses reject |
| M300, M400 | Bounded tone; motion-queue synchronization |

All other commands fail closed. G2/G3 return `arc.unsupported`; there is no chord
approximation. Examples of rejected commands: G28/G29, G10/G11 firmware retract,
G53-G59 workspaces, M200 volumetric extrusion, M205 jerk settings, M206/M218
offset changes, M302 cold-extrusion overrides, M486, Klipper macros, and nonzero
pressure advance. No arbitrary whitelist or manifest "ignore command" escape
exists. Supporting a new command requires implementation and mutation tests.

OctoPrint header start/stop pairs declare objects before the first executable
command. Body pairs must be balanced, non-nested, declared and exactly named.
Model ID/copy pairs are unique even under different names or leading zeros.
Footer `objects_info` names must agree with declarations and the manifest.
The verifier does not infer object membership from unlabelled extrusions such
as skirts or wipe towers. Firmware M486 and Klipper cancel-object dialects reject.

## What is and is not proven

| Property | v1 result under the declared input assumptions |
| --- | --- |
| Syntax, modes, numeric finiteness/sanity | Checked for every supported command |
| Build volume | Nozzle endpoints and therefore linear segments in a convex box |
| Tool envelope | Translation of the supplied AABB within that box; no part collision proof |
| Axis speed/feed/volumetric flow | Nominal requested motion bounds, including physical E scaling |
| Configured acceleration/axis limits | Commands cannot exceed fingerprint caps; dynamic profiles unproven |
| Temperature/cold extrusion | Command bounds and established lower bounds before positive E |
| Extrusion continuity | Logical resets, bounded deltas and cumulative retraction; actual deposited matter unproven |
| Object boundaries | Exact declared labels, unique IDs, balanced use and footer agreement |
| Fingerprint/program/work-packet binding | Exact program bytes plus canonical machine/manifest content, compiler identity and schema compatibility |
| End state/completeness | Bound program hash, unique terminal marker/final newline, closed labels, heater targets off |
| Conservative datum envelope | Axis-aligned protected/permitted envelope and expanded positive-E line sweeps when supplied |
| Swept deposited-part collision | Unproven |
| Support/dependency/contact contracts | Unproven |
| Artifact binding, calibration/authentication, report signing | Unproven |

Every report explicitly lists the unproven properties. Requesting any of them
in `required_properties` produces `rule.unproven` and rejects; omission does not
turn them into proven claims. A future geometry-contract schema will need exact
surface hashes, units/reference frame/transforms, tolerances, full tool solids
and independently reconstructed deposited matter. Those payloads do not exist
in final G-code. The current datum sidecar is deliberately conservative and
does not imply exact feature or collision proof.

Findings have one-based G-code lines and zero-based byte offsets. Line 0 denotes
input/schema/binding findings. Ordering is stable by line, code and message.
Parsing continues through the full file after a command failure for diagnostics;
`first_replay_error_line` marks where reliable replay stopped, and later state
diagnostics must not be treated as proofs. `final_state_trusted` is false on any
rejection. `replay_complete` describes interpretation, not physical certification.

## Qualification and remaining work

The seven M1 programs pass on an explicit **synthetic** 300 mm box, 200 mm/s feed,
200 mm3/s positive drive-flow cap and 0.4 mm wide tool AABB. These are regression
parameters, not calibration values. The original multi-material program extends
past its profile's 220 mm bed: a 220 mm fingerprint correctly rejects it starting
at line 204. No golden was changed and no limit was silently relaxed in an
existing printer fingerprint. The other six programs also pass a 220 mm check.

The suite preserves exact input bytes, checks deterministic reports and stable
finding order, verifies actual off/on production outputs, independently replays
seeded modal sequences with decimal arithmetic, and tests targeted malformed
input. It asserts full-file scanning and exact intended rejection codes for the
20 focused production mutations plus work-packet, path and datum mutations.
Hashes are rebound for physical mutations so a generic integrity failure cannot
conceal missing physics/state checks.

The M1.5 contract/integrity exit criterion is satisfied. Exact surface geometry,
deposited-part swept collision, dependencies/contacts, calibrated and
authenticated artifact packages, more firmware dialects, arcs/homing/tool
compensation, coverage-guided fuzzing and report signatures remain deferred to
later milestones. None of this authorizes thermal scheduling or generator
replacement.
