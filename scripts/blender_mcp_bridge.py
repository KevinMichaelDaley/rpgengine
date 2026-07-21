#!/usr/bin/env python3
"""
Minimal stdio MCP server bridging to the official Blender Labs MCP extension
(`lab_blender_org/mcp`, the add-on TCP server on 127.0.0.1:9876).

Why this exists: the add-on speaks a tiny custom protocol -- NUL-terminated JSON
frames of {"type": "execute", "code": <python>, "strict_json": bool}, where the
code runs inside Blender and must assign a `result` dict. Neither of the PyPI
"blender-mcp*" packages speaks it (they target other add-ons and hang), so this
bridge implements the MCP stdio side directly with zero dependencies.

Config (.mcp.json):
    { "mcpServers": { "blender": {
        "command": "python3",
        "args": ["/home/kmd/rpg/scripts/blender_mcp_bridge.py"] } } }

Tools exposed:
    blender_execute_python(code, timeout_s) -- run Python in Blender; assign
        `result = {...}` for structured output (auto-seeded to {} so scripts
        that only print still succeed). stdout/stderr are captured.

Ownership/side effects: one TCP connection per call (no shared state to wedge).
Errors from Blender come back as MCP tool errors with the full traceback.
"""
import json
import socket
import sys

BLENDER_ADDR = ("127.0.0.1", 9876)
PROTOCOL_VERSION = "2024-11-05"

TOOLS = [
    {
        "name": "blender_execute_python",
        "description": (
            "Execute Python code inside the running Blender instance (bpy is "
            "available). Assign a JSON-serializable dict to `result` to return "
            "structured data; anything printed is captured as stdout. Long "
            "operations (bakes, renders) are supported via timeout_s."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "code": {"type": "string", "description": "Python source to run in Blender"},
                "timeout_s": {
                    "type": "number",
                    "description": "Seconds to wait for Blender to finish (default 60)",
                },
            },
            "required": ["code"],
        },
    },
]


def blender_execute(code: str, timeout_s: float) -> dict:
    """One request per connection: connect, send NUL-framed JSON, read reply."""
    # Seed `result` so scripts that only print (or only mutate the scene)
    # satisfy the add-on's result-dict contract.
    framed = {
        "type": "execute",
        "code": "result = {}\n" + code,
        # strict_json False => the add-on repr()s non-serializable values
        # instead of erroring, which is friendlier for exploratory use.
        "strict_json": False,
    }
    with socket.create_connection(BLENDER_ADDR, timeout=10) as conn:
        conn.settimeout(timeout_s)
        conn.sendall(json.dumps(framed).encode("utf-8") + b"\0")
        buf = b""
        while b"\0" not in buf:
            chunk = conn.recv(65536)
            if not chunk:
                raise ConnectionError("Blender closed the connection mid-response")
            buf += chunk
    return json.loads(buf.split(b"\0", 1)[0])


def tool_result_from_response(resp: dict) -> dict:
    """Convert the add-on's response into MCP tool-call content."""
    parts = []
    if resp.get("status") == "ok":
        parts.append("result: " + json.dumps(resp.get("result", {}), indent=1))
    else:
        parts.append("ERROR: " + str(resp.get("message", "unknown error")))
    if resp.get("stdout"):
        parts.append("--- stdout ---\n" + resp["stdout"])
    if resp.get("stderr"):
        parts.append("--- stderr ---\n" + resp["stderr"])
    return {
        "content": [{"type": "text", "text": "\n".join(parts)}],
        "isError": resp.get("status") != "ok",
    }


def handle(msg: dict):
    """Return a response dict for a request, or None for notifications."""
    method = msg.get("method", "")
    msg_id = msg.get("id")
    if msg_id is None:
        return None  # notification (e.g. notifications/initialized)

    if method == "initialize":
        result = {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": {"name": "blender-lab-bridge", "version": "1.0.0"},
        }
    elif method == "tools/list":
        result = {"tools": TOOLS}
    elif method == "tools/call":
        params = msg.get("params", {})
        args = params.get("arguments", {}) or {}
        if params.get("name") != "blender_execute_python":
            return {
                "jsonrpc": "2.0", "id": msg_id,
                "error": {"code": -32602, "message": "unknown tool"},
            }
        try:
            resp = blender_execute(
                args.get("code", ""), float(args.get("timeout_s", 60)))
            result = tool_result_from_response(resp)
        except Exception as ex:  # surface connect/timeout errors to the model
            result = {
                "content": [{"type": "text", "text": (
                    "bridge error: {}: {}\n(Is Blender running with the MCP "
                    "add-on server started on 127.0.0.1:9876?)".format(
                        type(ex).__name__, ex))}],
                "isError": True,
            }
    elif method == "ping":
        result = {}
    else:
        return {
            "jsonrpc": "2.0", "id": msg_id,
            "error": {"code": -32601, "message": "method not found: " + method},
        }
    return {"jsonrpc": "2.0", "id": msg_id, "result": result}


def main() -> int:
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue
        resp = handle(msg)
        if resp is not None:
            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
