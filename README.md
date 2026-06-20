# libmake

[![codecov](https://codecov.io/github/nealpro/libmake/graph/badge.svg?token=5HVNS8R0EO)](https://codecov.io/github/nealpro/libmake)

Embeddable build automation tool similar to GNU `make`.

`libmake` currently implements a focused subset of POSIX `make`: explicit
rules loaded through the C API or a minimal Makefile parser, ordered
prerequisite traversal, mtime rebuild checks, recipe execution, and basic
diagnostics.

## POSIX error handling status (core)

Current status against POSIX `make` reference (proprietary, not in the repository):

- Implemented now:
  - Recursive prerequisite traversal and rebuild-on-stale/missing target logic.
  - Minimal Makefile loading for explicit target rules, prerequisites,
    semicolon recipes, and tab-indented recipes.
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
`libmake` against the same Makefile fixtures for:

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

## MCP server

An MCP (Model Context Protocol) server exposes the build graph and build
execution to LLMs, enabling agentic build debugging and tool orchestration.

### Setup

```sh
uv venv mcp/.venv
source mcp/.venv/bin/activate
uv pip install -r mcp/requirements.txt
```

### Running

Test interactively with the MCP Inspector:

```sh
mcp dev mcp/server.py
```

Configure in Claude Code or Claude Desktop (`settings.json`):

```json
{
  "mcpServers": {
    "libmake": {
      "command": "mcp/.venv/bin/python3",
      "args": ["mcp/server.py"],
      "cwd": "/path/to/libmake"
    }
  }
}
```

Set `LIBMAKE_PROJECT_DIR` to override the working directory if needed.

### Tools

| Tool | Description |
|------|-------------|
| `list_targets` | List all build targets with dependency and command counts |
| `show_target` | Show full detail for a target (commands, deps, file mtime) |
| `build` | Trigger a build and return exit code, stdout, stderr, duration |
| `dry_run` | Explain what a build would do without executing |
| `visualize_graph` | Generate a Mermaid diagram of the dependency graph |
| `clean` | Run `libmake clean` to remove build artifacts |

### Resources

| URI | Description |
|-----|-------------|
| `libmake://graph` | Full build graph as JSON |
| `libmake://build-log` | Most recent build output |
| `libmake://source/{filename}` | Read a source file from `src/` |

### CLI introspection flags

The MCP server relies on two flags added to the `libmake` binary:

- `./libmake --dump-graph` — serialize the build graph as JSON
- `./libmake --dry-run <target>` — explain rebuild decisions as JSON
- `./libmake -f Makefile [target]` — load a Makefile subset and build the
  requested or first non-special target

# Code Coverage

[![codecov](https://codecov.io/github/nealpro/libmake/graphs/sunburst.svg?token=5HVNS8R0EO)](https://codecov.io/github/nealpro/libmake)
