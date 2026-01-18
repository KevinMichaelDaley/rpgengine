## Issue Tracking and Task Management

This project uses **bd (beads)** for issue tracking and task management.
Run `bd prime` for workflow context, or install hooks (`bd hooks install`) for auto-injection.

**Quick reference:**
- `bd ready` - Find unblocked work
- `bd create "Title" --type task --priority 2` - Create issue
- `bd close <id>` - Complete work
- `bd sync` - Sync with git (run at session end)

For full workflow details: `bd prime`
ALWAYS use `bd show` to read the full, extended description of a bead before trying to implement or continue implementing. 
Explanations of how to do the tasks or why they exist can often be found in other places in ref/ with descriptive filenames; for example, check ref/architecture.md for architectural guidelines. 

# CRITICAL DIRECTIVE (C VERSION)
## Strict Test-Driven Development + Extreme Modularity

### 0. LANGUAGE TARGET
- **Language:** C (C11)
- **Build expectation:** Tests may fail to compile in Phase 1 — this is required.
- **Rule:** No implementation code before tests.

---

## 1. MANDATORY WORKFLOW: TDD & NO SHORTCUTS

### Phase 1: The Test Battery (RED)
Write all unit and regression tests **before** any production `.c/.h` files exist.

- **Happy Path:** expected behavior
- **Edge Cases:** boundaries (0, empty, max, nulls, overflow-adjacent)
- **Failure Modes:** invalid inputs, error propagation

The code is expected not to compile yet.

### Phase 2: Full Implementation (GREEN)
- Implement only what tests require.
- **NO SKELETONS:** no TODOs, no stub returns.
- **NO BACKFILLING:** tests define the API.
- Runtime errors must be handled explicitly (no crashes unless tests demand it).

### Phase 3: Refactor & Verify
- Refactor without changing behavior.
- Re-check all edge and failure cases.
- Enforce file structure rules.

---

## 2. FILE STRUCTURE & GRANULARITY (EXTREME MODULARITY)

### Directory-first Design
Prefer deep hierarchies:
- ✅ `src/physics/collision/broadphase/grid.c`
- ❌ `src/physics/broadphase.c`

### Header / Module Ownership
- One public header per module.
- Public API only in headers.
- Private helpers stay in `.c` files.

### Hard Limits

#### 2-Type Rule (Headers)
A single public header must expose **no more than 2 public types**
(structs, enums, or typedefs).  The exception to this rule is forward declarations; you can use as many forward declarations as are necessary.

#### 4-Function Rule (Source Files)
A single `.c` file must contain **no more than 4 non-static functions**.
Static helpers are allowed but should be minimal.  It is preferable to create a new file than to suppress features or lengthen function bodies to satisfy this rule, especially for unit tests.  

### Module Wiring Rule
Whenever a new module is created:
- Explicitly show the `#include` added to the parent aggregator header.

---

## 3. CODE QUALITY & C IDIOMS

### Error Handling
- No `abort()`, `exit()`, or runtime `assert()` unless tests demand it.
- Prefer `enum` status codes or `bool` + out-parameters.
- All public APIs must document:
  - Ownership rules
  - Nullability
  - Error semantics
  - Side effects
### Style
- Use variable and function names that are descriptive and indicate their purpose.  Use obvious syntax and avoid
unnecessary brevity when it detracts from readability.  
- Comment code liberally.  Keep comments up-to-date.

### Memory Rules
- Ownership must be explicit.
- No hidden global state unless tests require it.
- Prefer explicit context / allocator objects.

### Safety
- No UB-prone casts.
- No compiler extensions.
- Must be clean under `-Wall -Wextra -Wpedantic`.

### Documentation
All public APIs must be documented using Doxygen-style comments.

---

## 4. DEPENDENCY CONSTRAINTS

### Allowed
- Standard C library headers.
- POSIX headers and libraries (sockets, etc).
- OpenGL, GLEW, and SDL2 (prefer system packages to submodules or including third party code directly in the repo).

### Forbidden
- Third-party libraries unless explicitly whitelisted.
- Hidden framework dependencies.

### Testing
- Use a minimal custom test harness unless otherwise specified.

---

## 5. REQUIRED RESPONSE FORMAT

For every feature request, respond in this exact order:

1. **Test Plan**
   - Happy / Edge / Fail cases

2. **File: `tests/<feature>_tests.c`**
   - Full test code first (may not compile yet)

3. **Plan**
   - List all `.h` and `.c` files to be created
   - Show aggregator `#include` changes

4. **Implementation**
   - Full headers and sources
   - Must satisfy tests and structural rules

5. **Verification**
   - Confirm test coverage
   - Confirm structure and dependency constraints
