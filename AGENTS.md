# Agent Instructions

This project uses **tk** (ticket) for issue tracking. Run `tk help` to see available commands.

## Quick Reference

```bash
tk ready              # Find available work (deps resolved)
tk show <id>          # View issue details
tk status <id> in_progress  # Claim work
tk close <id>         # Complete work
tk dep tree <id>      # View dependency tree
tk blocked            # List tickets with unresolved deps
```

## Ticket Commands

```bash
# Listing
tk ls                              # All tickets
tk ls --status=open                # Open tickets only
tk ready -T procgen                # Ready procgen tickets
tk blocked                         # Tickets waiting on deps

# Viewing / editing
tk show <id>                       # Full ticket view
tk edit <id>                       # Open in $EDITOR
tk add-note <id> "note text"       # Append timestamped note

# Dependencies
tk dep <id> <dep-id>               # Add dependency (id depends on dep-id)
tk dep tree <id>                   # Show dependency tree
tk dep cycle                       # Find dependency cycles

# Status transitions
tk status <id> open                # Reopen a closed ticket
tk status <id> in_progress         # Start work
tk close <id>                      # Mark complete
```

## Test-Driven Development (TDD)

**ALL work MUST follow RED-GREEN-REFACTOR:**

### Phase 1: RED (Write failing tests first)

1. Read the ticket's acceptance criteria and design notes
2. Create the test file in `tests/<subsystem>/` following existing patterns:
   - Include the test macros: `RUN()`, `ASSERT_TRUE()`, `ASSERT_EQ()`, `ASSERT_INT_EQ()`
   - Use `g_pass`/`g_fail` counters and `PASS()` macro
   - Each `test_*()` function tests one behavior
3. Write tests that validate the ticket's acceptance criteria
4. Verify tests **compile** (`make <test_target>`) and **fail** (RED)
5. Commit RED tests: `git add tests/... && git commit -m "[RED] <ticket-id>: failing tests for <description>"`

### Phase 2: GREEN (Minimal implementation)

1. Create/modify the implementation files in `src/<subsystem>/` and `include/ferrum/<subsystem>/`
2. Follow existing code conventions (see below)
3. Write the **minimal** code needed to make tests pass
4. Run tests: `make <test_target> && ./build/<test_binary>`
5. All tests must pass with zero failures
6. Commit: `git commit -m "[GREEN] <ticket-id>: implementation passing tests"`

### Phase 3: REFACTOR (Clean up)

1. Extract duplicated code into helper functions
2. Remove magic numbers, add proper constants
3. Ensure no warnings (`-Wall -Wextra`)
4. Run ALL tests (not just the new ones): `make test`
5. Commit: `git commit -m "[REFACTOR] <ticket-id>: cleanup after tests"`

### Test File Template

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/<subsystem>/<header>.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    fn(); \
    printf("OK   %s\n", #fn); \
} while (0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_fail++; \
        return; \
    } \
} while (0)

#define ASSERT_EQ(a, b)   ASSERT_TRUE((a) == (b))
#define ASSERT_INT_EQ(a, b) ASSERT_TRUE((int)(a) == (int)(b))
#define PASS() g_pass++

static void test_description_of_behavior(void) {
    // Arrange
    // Act
    // Assert
    ASSERT_EQ(result, expected);
    PASS();
}

int main(void) {
    printf("=== <Subsystem> Tests ===\n\n");
    RUN(test_description_of_behavior);
    /* ... more RUN() calls ... */
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
```

## Code Conventions

- **Language**: C11 (`-std=c11`), no compiler extensions unless gated behind `#ifdef`
- **Naming**: `snake_case` for functions, `snake_case` for types with `_t` suffix, `UPPER_SNAKE` for macros/enums
- **Prefixes**: Public API uses `fr_` prefix (e.g., `fr_vec3_add`). Subsystem-internal functions use subsystem prefix (e.g., `procgen_tokenize_*`)
- **Headers**: `#pragma once` alternative: `#ifndef FERRUM_SUBSYSTEM_HEADER_H` / `#define` / `#endif`
- **Includes**: System headers first, then project headers in alphabetical order
- **Error handling**: Return `bool` or `int` (0=success, -1=error). Use `char *err_buf, uint32_t err_cap` pattern for descriptive error messages
- **Memory**: Arena/bump allocators for frame lifetime, pool for fixed-size objects. No `malloc` in hot paths
- **Comments**: Doxygen `/** ... */` on all public functions. No inline comments unless code is non-obvious
- **File length**: Target <500 lines per .c file. Split when it grows beyond that
- **No dead code**: Remove `#if 0` blocks before committing
- **Zero warnings**: `-Wall -Wextra` must be clean

## Build Commands

```bash
make all                 # Full build (libheadless.a + all binaries)
make test                # Build and run all headless tests
make test_timeout        # Tests with per-test timeout (20s default)
make test_renderer       # Renderer tests (requires SDL2/OpenGL)

# Procgen-specific
make procgen             # Build only procgen objects
make procgen-test        # Build and run all procgen tests
make procgen-bench       # Build and run procgen benchmarks

# Individual test targets
make pNNN_*_tests        # Build specific test binary
./build/pNNN_*_tests     # Run specific test binary
```

## Subsystem: Procedural Dungeon Grammar (`src/procgen/`)

Key concepts:
- **Grammar**: A compiled C module implementing `tokenize()` + `rasterize()`. Registered in grammar registry.
- **Token string**: The "DNA" of a dungeon level — a linear sequence of typed tokens (ROOM_QUAD, CORRIDOR_H, SPAWN, MARKER, etc.)
- **Layout** (`fr_dungeon_layout_t`): Intermediate representation with rooms, corridors, ramps, markers, nav graph
- **Architect VLM**: Uses LLM infrastructure (engine_settings) to generate token strings from natural language
- **Critic**: Automated playtester using NitroGen vision-action model, with engine hooks for death/marker detection
- **TDD**: Every procgen ticket follows RED-GREEN-REFACTOR. Tests go in `tests/procgen/`.

See `design/procgen_dungeon_grammar.md` for the full architecture document.

**When ending a work session**, you MUST complete ALL steps below. Work is NOT complete until `git push` succeeds.

**MANDATORY WORKFLOW:**

1. **File issues for remaining work** - Create issues for anything that needs follow-up
2. **Run all tests** To be sure what we did worked, and nothing broke.
3. **Update issue status** - Close finished work, update in-progress items
4. **PUSH TO REMOTE** - This is MANDATORY:
   ```bash
   git pull --rebase
   git push
   git status  # MUST show "up to date with origin"
   ```
5. **Clean up** - Clear stashes, prune remote branches
6. **Verify** - All changes committed AND pushed
7. **Hand off** - Provide context for next session

**CRITICAL RULES:**
- Work is NOT complete until `git push` succeeds
- NEVER stop before pushing - that leaves work stranded locally
- NEVER say "ready to push when you are" - YOU must push
- If push fails, resolve and retry until it succeeds

