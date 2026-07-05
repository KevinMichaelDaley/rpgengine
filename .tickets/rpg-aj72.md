---
id: rpg-aj72
status: closed
deps: []
links: []
created: 2026-07-05T06:24:23Z
type: task
priority: 0
assignee: KMD
parent: rpg-0wlk
tags: [procgen]
---
# srd-001: extern/SymX submodule + build integration

**DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!**



Add extern/SymX as a git submodule and wire it into the CMake/Makefile build system. Create a minimal smoke-test target that verifies SymX compiles and links correctly.

**RED-phase tests:**
- tests/procgen/srd/srd_build_test.cpp — include SymX headers, create a Scalar, evaluate x+2, verify compilation and correct result

## Design

Add to CMakeLists.txt:
```cmake
add_subdirectory(extern/SymX)
target_link_libraries(headless PRIVATE SymX::SymX)
```

SymX requires: C++17 compiler, CMake 3.15+, Eigen.

## Acceptance Criteria

1. `git submodule status extern/SymX` returns valid commit
2. `make srd_build_test` compiles and links
3. `./build/srd_build_test` returns 0
4. `make all` still builds demo_client without SymX dependency (SymX only linked where needed)

