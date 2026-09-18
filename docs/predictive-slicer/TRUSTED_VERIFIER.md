# Independent trusted verifier

## Purpose

The verifier answers one narrow question: is this final machine program allowed
to execute under this machine fingerprint and manufacturing contract? It does not
generate paths, optimize them, or repair failures.

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
5. Swept tool/envelope collision against predicted deposited geometry.
6. Object identity and exclude-object boundary preservation.
7. Protected datum and forbidden-region violations.
8. Required dependencies and unsupported deposition.
9. Contact lower/upper contracts where sufficient evidence exists.
10. Exact binding to fingerprint, compiler manifest, and artifact report.

## Architecture

```text
G-code + manifest + fingerprint + contracts
                  |
             strict parser
                  |
        independent state replay
                  |
 geometry / motion / contract checks
                  |
        signed verifier report
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
