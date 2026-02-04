---
id: rust-rpg-o4i.2
status: closed
deps: [rust-rpg-o4i.1]
links: []
created: 2026-01-18T10:58:28.837788021-08:00
type: task
priority: 2
parent: rust-rpg-o4i
---
# P_004.2 Shader compile/link + error logs

## P_004.2 Shader compile/link + error logs

### Goal
Implement shader compilation/linking with explicit error logging and truncation behavior.

### Scope
- Compile vertex/fragment shaders; link into program.
- Capture and NUL-terminate shader/program error logs with truncation.
- Ensure bind uses explicit GL calls (no implicit global state).

### Tests
- Shader compile success path; program handle stored; bind calls correct GL functions.
- Shader compile failure captures error log.
- Program link failure captures error log.
- Error log truncation regression.



