"""MCP server exposing libmake build graphs and error logs to LLMs."""

import asyncio
import json
import os
import time
from pathlib import Path

from mcp.server.fastmcp import FastMCP

mcp = FastMCP("libmake")

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

PROJECT_DIR = Path(os.environ.get("LIBMAKE_PROJECT_DIR", os.getcwd()))
PROVIDER = Path(os.environ.get("LIBMAKE_PROVIDER", PROJECT_DIR / "libmake"))
SRC_DIR = PROJECT_DIR / "src"
TIMEOUT = 30  # seconds

# In-memory build log (most recent)
_last_build_log: dict | None = None


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


async def _run(
    *args: str, timeout: int = TIMEOUT
) -> tuple[int, str, str]:
    """Run a subprocess and return (exit_code, stdout, stderr)."""
    proc = await asyncio.create_subprocess_exec(
        *args,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
        cwd=str(PROJECT_DIR),
    )
    try:
        stdout, stderr = await asyncio.wait_for(
            proc.communicate(), timeout=timeout
        )
    except asyncio.TimeoutError:
        proc.kill()
        await proc.communicate()
        return -1, "", f"Process timed out after {timeout}s"
    return proc.returncode, stdout.decode(), stderr.decode()


def _provider_args(*args: str, makefile: str | None = None) -> list[str]:
    cmd = [str(PROVIDER)]
    if makefile:
        cmd.extend(["-f", makefile])
    cmd.extend(args)
    return cmd


def _provider_missing_error() -> str:
    return (
        f"libmake provider not found at {PROVIDER}. "
        "Compile it first: cc -o libmake src/main.c src/dag.c "
        "src/exec.c src/libmake.c"
    )


async def _dump_graph(makefile: str | None = None) -> dict:
    """Run provider --dump-graph and return parsed JSON."""
    if not PROVIDER.exists():
        raise RuntimeError(_provider_missing_error())
    rc, out, err = await _run(*_provider_args("--dump-graph", makefile=makefile))
    if rc != 0:
        raise RuntimeError(f"--dump-graph failed (exit {rc}): {err}")
    return json.loads(out)


async def _dump_makefile(makefile: str | None = None) -> str:
    """Run provider --dump-makefile and return Makefile text."""
    if not PROVIDER.exists():
        raise RuntimeError(_provider_missing_error())
    rc, out, err = await _run(
        *_provider_args("--dump-makefile", makefile=makefile)
    )
    if rc != 0:
        raise RuntimeError(f"--dump-makefile failed (exit {rc}): {err}")
    return out


# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------


@mcp.tool()
async def list_targets(makefile: str | None = None) -> str:
    """List all build targets with their dependency and command counts."""
    graph = await _dump_graph(makefile)
    rows = []
    for node in graph["nodes"]:
        rows.append(
            {
                "name": node["name"],
                "num_deps": len(node["deps"]),
                "num_commands": len(node["commands"]),
            }
        )
    return json.dumps(rows, indent=2)


@mcp.tool()
async def show_target(target: str, makefile: str | None = None) -> str:
    """Show full detail for a single build target: commands, deps, and file mtime."""
    graph = await _dump_graph(makefile)
    for node in graph["nodes"]:
        if node["name"] == target:
            # Add file mtime if the target file exists
            target_path = PROJECT_DIR / target
            if target_path.exists():
                node["mtime"] = target_path.stat().st_mtime
                node["mtime_iso"] = time.strftime(
                    "%Y-%m-%dT%H:%M:%S",
                    time.localtime(node["mtime"]),
                )
            else:
                node["mtime"] = None
            return json.dumps(node, indent=2)
    return json.dumps({"error": f"target '{target}' not found"})


@mcp.tool()
async def build(target: str, makefile: str | None = None) -> str:
    """Trigger a build for the given target and return structured results."""
    global _last_build_log
    if not PROVIDER.exists():
        return json.dumps(
            {
                "error": "libmake provider not found. Compile it first."
            }
        )
    start = time.monotonic()
    rc, out, err = await _run(*_provider_args(target, makefile=makefile))
    duration = time.monotonic() - start
    result = {
        "exit_code": rc,
        "stdout": out,
        "stderr": err,
        "duration_seconds": round(duration, 3),
    }
    _last_build_log = result
    return json.dumps(result, indent=2)


@mcp.tool()
async def dry_run(target: str, makefile: str | None = None) -> str:
    """Explain what a build would do without executing anything."""
    if not PROVIDER.exists():
        return json.dumps(
            {
                "error": "libmake provider not found. Compile it first."
            }
        )
    rc, out, err = await _run(
        *_provider_args("--dry-run", target, makefile=makefile)
    )
    if rc != 0:
        return json.dumps({"error": err.strip(), "exit_code": rc})
    return out  # already JSON


@mcp.tool()
async def dump_makefile(makefile: str | None = None) -> str:
    """Emit the provider graph as a Makefile."""
    try:
        return await _dump_makefile(makefile)
    except RuntimeError as exc:
        return json.dumps({"error": str(exc)})


@mcp.tool()
async def visualize_graph(
    target: str | None = None, makefile: str | None = None
) -> str:
    """Generate a Mermaid diagram of the build graph (or a subgraph rooted at target)."""
    graph = await _dump_graph(makefile)
    nodes_by_name = {n["name"]: n for n in graph["nodes"]}

    if target:
        # BFS to collect reachable subgraph
        if target not in nodes_by_name:
            return json.dumps({"error": f"target '{target}' not found"})
        visited = set()
        queue = [target]
        while queue:
            name = queue.pop(0)
            if name in visited:
                continue
            visited.add(name)
            node = nodes_by_name.get(name)
            if node:
                queue.extend(node["deps"])
        subset = [n for n in graph["nodes"] if n["name"] in visited]
    else:
        subset = graph["nodes"]

    lines = ["graph TD"]
    for node in subset:
        for dep in node["deps"]:
            # Sanitize names for Mermaid (replace dots/slashes)
            src = node["name"].replace(".", "_").replace("/", "_")
            dst = dep.replace(".", "_").replace("/", "_")
            lines.append(
                f"  {src}[\"{node['name']}\"] --> {dst}[\"{dep}\"]"
            )
    return "\n".join(lines)


@mcp.tool()
async def clean(makefile: str | None = None) -> str:
    """Run 'libmake clean' to remove build artifacts."""
    if not PROVIDER.exists():
        return json.dumps(
            {
                "error": "libmake provider not found. Compile it first."
            }
        )
    rc, out, err = await _run(*_provider_args("clean", makefile=makefile))
    return json.dumps(
        {"exit_code": rc, "stdout": out, "stderr": err}, indent=2
    )


# ---------------------------------------------------------------------------
# Resources
# ---------------------------------------------------------------------------


@mcp.resource("libmake://graph")
async def graph_resource() -> str:
    """The full build graph as JSON."""
    graph = await _dump_graph()
    return json.dumps(graph, indent=2)


@mcp.resource("libmake://makefile")
async def makefile_resource() -> str:
    """The provider graph emitted as a Makefile."""
    return await _dump_makefile()


@mcp.resource("libmake://build-log")
async def build_log_resource() -> str:
    """The most recent build log."""
    if _last_build_log is None:
        return json.dumps({"message": "No builds have been run yet."})
    return json.dumps(_last_build_log, indent=2)


@mcp.resource("libmake://source/{filename}")
async def source_resource(filename: str) -> str:
    """Read a source file from src/."""
    # Prevent path traversal
    safe = Path(filename).name
    path = SRC_DIR / safe
    if not path.exists():
        return json.dumps({"error": f"File not found: src/{safe}"})
    return path.read_text()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    mcp.run(transport="stdio")
