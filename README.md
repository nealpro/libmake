# libmake

Embeddable build automation tool similar to GNU `make`.

`libmake` currently implements a focused subset of POSIX `make`: explicit
rules, ordered prerequisite traversal, mtime rebuild checks, recipe execution,
and basic diagnostics.

## POSIX error handling status (core)

Current status against `make.txt` (POSIX `make` reference):

- Implemented now:
  - Recursive prerequisite traversal and rebuild-on-stale/missing target logic.
  - Termination when recipe command returns non-zero.
  - Diagnostics for missing rule and circular dependency detection.

- Missing / not yet POSIX-complete:
  - Command prefixes: `-` (ignore error), `@` (silent), `+` (force execution).
  - Options: `-i`, `-k`, `-S`, `-n`, `-q`, `-s`, `-t`, `-r`, `-p`, `-e`, `-f`.
  - Special targets: `.POSIX`, `.IGNORE`, `.SILENT`, `.PRECIOUS`, `.DEFAULT`,
    `.SUFFIXES`, `.SCCS_GET` behavior.
  - Required async signal cleanup semantics for in-progress targets.
  - Full macro expansion rules and include processing.

- Internal robustness gaps to fix early:
  - Allocation failures in DAG growth paths are not consistently surfaced.
  - `lmk_rule()` currently does not return/report insertion failures.

## Differential testing strategy

A practical way to progress toward POSIX usability is differential testing:
compare behavior of system `make` and `libmake` on the same intended build
logic, starting with core features used in most projects.

This repository now includes a baseline harness:

- `tests/posix-core/run.sh`
- `tests/posix-core/lmk_runner.c`

The harness builds a tiny `libmake` runner and checks both `make` and
`libmake` for:

- Basic build success
- Up-to-date no-op behavior
- Rebuild when a prerequisite becomes newer
- Missing target failure path
- Recipe failure path

Run it with:

```sh
sh tests/posix-core/run.sh
```

This creates an executable baseline to expand incrementally with additional
POSIX features as they are implemented.
