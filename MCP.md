# libmake MCP Server

An MCP (Model Context Protocol) server that exposes libmake graph providers to LLMs. It lets an AI agent inspect the build graph, query targets, run builds, emit Makefiles, and read source files — all without touching the shell directly.

## Setup

The server lives in `mcp/server.py` and requires Python with the `mcp` package.

**Install dependencies:**
```sh
cd mcp && python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
```

**Claude Code configuration** (`.mcp.json` in the project root):
```json
{
  "mcpServers": {
    "libmake": {
      "command": "mcp/.venv/bin/python3",
      "args": ["mcp/server.py"],
      "cwd": "/Users/neal/Projects/libmake"
    }
  }
}
```

**Prerequisite:** A libmake graph-provider executable must be compiled before most tools will work. The default provider is this repo's `./libmake`:
```sh
cc -o libmake src/main.c src/dag.c src/exec.c src/libmake.c
```

The server reads:

- `LIBMAKE_PROJECT_DIR` — working directory and source tree root (defaults to the current working directory)
- `LIBMAKE_PROVIDER` — graph-provider executable (defaults to `$LIBMAKE_PROJECT_DIR/libmake`)

A provider is expected to support:

```sh
provider --dump-graph
provider --dump-makefile
provider --dry-run <target>
provider <target>
```

The default provider also accepts `-f <makefile>` before these actions, allowing MCP tools to inspect a supported Makefile subset without making Makefiles the primary model.

## Tools

### `list_targets`

Lists all build targets with their dependency and command counts.

**Parameters:**
- `makefile` (string, optional) — load this Makefile through the provider before listing targets

**Returns:** JSON array of objects:
```json
[
  { "name": "all",     "num_deps": 1, "num_commands": 0 },
  { "name": "libmake", "num_deps": 4, "num_commands": 1 },
  { "name": "main.o",  "num_deps": 2, "num_commands": 1 }
]
```

---

### `show_target`

Shows full detail for a single target: its commands, dependencies, and the file's last modification time (if it exists on disk).

**Parameters:**
- `target` (string) — target name, e.g. `"libmake"` or `"main.o"`
- `makefile` (string, optional) — load this Makefile through the provider first

**Returns:** JSON object:
```json
{
  "name": "libmake",
  "commands": ["cc -o libmake main.o dag.o exec.o libmake.o"],
  "deps": ["main.o", "dag.o", "exec.o", "libmake.o"],
  "mtime": 1774107202.423,
  "mtime_iso": "2026-03-21T11:33:22"
}
```

`mtime` is `null` when the output file doesn't exist yet. Returns `{"error": "target '...' not found"}` for unknown targets.

---

### `dry_run`

Explains what a build would do without executing any commands. Uses libmake's built-in `--dry-run` mode.

**Parameters:**
- `target` (string) — target to explain
- `makefile` (string, optional) — load this Makefile through the provider first

**Returns:** JSON with `plan` (steps that would run) and `skipped` (up-to-date targets):
```json
{
  "target": "all",
  "plan": [
    { "node": "main.o",    "action": "rebuild", "reason": "dependency_newer", "dep": "src/main.c" },
    { "node": "libmake.o", "action": "rebuild", "reason": "dependency_newer", "dep": "src/libmake.c" }
  ],
  "skipped": [
    { "node": "exec.o",   "reason": "up_to_date" },
    { "node": "libmake",  "reason": "up_to_date" }
  ]
}
```

Possible `reason` values: `file_missing`, `dependency_newer`, `up_to_date`.

---

### `build`

Triggers a build for the given target and returns the result. The output is also stored in-memory and accessible via the `libmake://build-log` resource.

**Parameters:**
- `target` (string) — target to build, e.g. `"all"`, `"main.o"`
- `makefile` (string, optional) — load this Makefile through the provider first

**Returns:** JSON with exit code, stdout, stderr, and duration:
```json
{
  "exit_code": 0,
  "stdout": "\tcc -c src/main.c -o main.o\n\tcc -o libmake main.o dag.o exec.o libmake.o\n",
  "stderr": "",
  "duration_seconds": 0.51
}
```

---

### `dump_makefile`

Emits the provider graph as a Makefile in the supported subset. This is useful when the provider constructs its graph through embedded C API calls and the caller wants a `make`-compatible differential artifact.

**Parameters:**
- `makefile` (string, optional) — load this Makefile through the provider first, then re-emit the loaded graph

**Returns:** Makefile text, or a JSON error object.

---

### `clean`

Runs the `clean` target to remove build artifacts (`libmake` binary and `*.o` files).

**Parameters:**
- `makefile` (string, optional) — load this Makefile through the provider first

**Returns:** Same shape as `build` — exit code, stdout, stderr.

---

### `visualize_graph`

Generates a [Mermaid](https://mermaid.js.org/) `graph TD` diagram of the full build graph, or a subgraph rooted at a specific target.

**Parameters:**
- `target` (string, optional) — if provided, only the subgraph reachable from this target is included
- `makefile` (string, optional) — load this Makefile through the provider first

**Returns:** A Mermaid diagram string. Node names with `.` or `/` are sanitized to `_` for Mermaid compatibility, but the original names appear as labels.

Example output for `target="libmake"`:
```
graph TD
  libmake["libmake"] --> main_o["main.o"]
  libmake["libmake"] --> dag_o["dag.o"]
  main_o["main.o"] --> src_main_c["src/main.c"]
  ...
```

Returns `{"error": "target '...' not found"}` for unknown targets.

## Resources

Resources are read-only data streams. They can be fetched with `ReadMcpResourceTool`.

### `libmake://graph`

The full build graph as JSON, identical to what `libmake --dump-graph` produces. Requires the binary to exist.

```json
{
  "nodes": [
    {
      "name": "all",
      "commands": [],
      "deps": ["libmake"]
    },
    ...
  ]
}
```

### `libmake://makefile`

The default provider graph emitted as a Makefile, identical to what `libmake --dump-makefile` produces.

```make
all: libmake

libmake: main.o dag.o exec.o libmake.o
	cc -o libmake main.o dag.o exec.o libmake.o
```

### `libmake://build-log`

The result of the most recent `build` tool call (in-memory, not persisted across server restarts). Returns `{"message": "No builds have been run yet."}` if no build has been triggered in the current session.

### `libmake://source/{filename}`

Reads a source file from `src/`. Path traversal is prevented — only the basename of `filename` is used.

Example URI: `libmake://source/dag.h`

Returns the raw file contents, or `{"error": "File not found: src/..."}` if the file doesn't exist.

## Error Handling

- All tools return JSON error objects (`{"error": "..."}`) rather than raising exceptions, so the LLM always gets a parseable response.
- If the graph provider is missing, tools that need it return an error message with the compile command for the default provider.
- Subprocess calls time out after 30 seconds.
