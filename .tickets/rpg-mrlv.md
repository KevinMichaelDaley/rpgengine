---
id: rpg-mrlv
status: closed
deps: [rpg-gc2a]
links: []
created: 2026-07-04T20:41:08Z
type: task
priority: 0
assignee: KMD
parent: rpg-gc2a
tags: [procgen, integration, build, makefile]
---
# procgen-9e: Build system integration

## Design

Wire procgen into the build system: add src/procgen/*.c to SRC_HEADLESS in Makefile, add tests/procgen/* to test target, add include/ferrum/procgen/ to include path (already in -Iinclude). Update make test to run procgen tests. Add make procgen target. Ensure TRACY=1 integration for profiling. Add make procgen-clean target.

## Acceptance Criteria

- make test builds and runs all procgen tests\n- make procgen builds only procgen objects\n- libheadless.a includes procgen symbols\n- All procgen tests pass in CI\n- TRACY=1 builds with procgen profiling zones\n- No warnings with -Wall -Wextra\n- make clean removes procgen objects

