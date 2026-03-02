---
id: rpg-2hjw
status: open
deps: []
links: []
created: 2026-03-02T00:27:16Z
type: chore
priority: 3
assignee: KMD
tags: [rename, refactor, breaking]
---
# Rename: ferrum→talarium, demo_{server,client}→src/game, libheadless→libtalarium_engine

Global rename across the entire codebase:

1. **s/ferrum/talarium** — namespace rename in all headers, sources, tests, includes, guards, Makefile, docs
   - Rename include/ferrum/ → include/talarium/
   - Update all #include "ferrum/..." → #include "talarium/..."
   - Update all header guards FERRUM_* → TALARIUM_*
   - Update all FR_/fr_ prefixed symbols where they derive from 'ferrum' (e.g. fr_topic_channel → needs review)
   - ~970 files affected, ~264 headers

2. **s/demo_server/server, s/demo_client/client** — rename binaries and move source files
   - Move tests/examples/demo_server.c → src/game/server.c
   - Move tests/examples/demo_client.c → src/game/client.c
   - Update Makefile targets: build/demo_server → build/server, build/demo_client → build/client
   - Update all references in Makefile, docs, scripts

3. **s/libheadless/libtalarium_engine** — rename the static library
   - build/libheadless.a → build/libtalarium_engine.a
   - Update SRC_HEADLESS → SRC_ENGINE or similar
   - Update all Makefile references (~368 uses)

Approach: Use git mv for directory renames, sed for bulk text replacement, manual fixup for edge cases. Do in phases to keep commits reviewable.

CAUTION: fr_ prefix on public API functions (fr_topic_channel, fr_rudp_*, fr_server_*, fr_priority_*) is a design choice — review whether these should become tal_ or stay as-is. The user should decide.

## Acceptance Criteria

- All #include paths use talarium/ not ferrum/
- Header guards use TALARIUM_ not FERRUM_
- Binaries are build/server and build/client from src/game/
- Library is build/libtalarium_engine.a
- All tests pass (except pre-existing p108)
- No stale ferrum/ directory remains in include/
- Demo server smoke test passes under new name

