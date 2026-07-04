# Procedural Dungeon Grammar + Dual-VLM Architect/Critic

## Overview

A **multi-grammar procedural dungeon system** with a **dual-VLM pipeline**: an architect VLM generates level layouts from natural language descriptions, and a critic VLM (built atop NitroGen) playtests the result through embodied play, mapping deaths and verifying the player can reach all key markers.

## Architecture

```
User Prompt → Architect VLM → Token String → Tokenizer → Tokens → Rasterizer
                                                         ↓
                                         reject if unparseable
                                                              ↓
                                              fr_dungeon_layout_t → Serializer
                                                              ↓               ↓
                                                        JSON Level    Critic Hooks
                                                                           ↓
                                                              Critic Runtime ← NitroGen
                                                                           ↓
                                                                   Summary Report
```

---

## 1. Shape Grammar Framework

### 1a. Grammar Definition Format

Each grammar is a compiled C module implementing:

```c
typedef struct procgen_grammar {
    const char *name;
    uint32_t    version;
    bool (*tokenize)(const char *input, procgen_token_t *buf,
                     uint32_t buf_cap, uint32_t *out_count,
                     char *err_buf, uint32_t err_cap);
    bool (*rasterize)(const procgen_token_t *tokens, uint32_t count,
                      fr_dungeon_layout_t *layout);
    const char *vlm_system_prompt_fragment;
    const char **known_markers;
    uint32_t    known_marker_count;
} procgen_grammar_t;
```

### 1b. Token Types

```
ROOM_QUAD, ROOM_PENT     — 4/5-sided room polytopes
CORRIDOR_H, CORRIDOR_V   — horizontal/vertical corridors
CORRIDOR_DIAG            — 45° / 30°-60° diagonal corridors
RAMP_UP, RAMP_DOWN       — floor height changes
DOOR, WINDOW             — openings between rooms
SPAWN, MARKER            — spawn point, key waypoints
BLOCK, EBLOCK            — nesting/grouping
```

---

## 2. Token String Format (Level DNA)

### Example

```
@grammar blockout v1

BLOCK
  ROOM_QUAD  x=0 y=0 w=20 h=16 floor_z=0 ceil_z=6  name=entrance
  SPAWN     x=0 y=0 z=1
  MARKER    x=10 y=0 z=1  name=first_encounter

  CORRIDOR_H  from=(20,0) to=(40,0) w=6 floor_z=0 ceil_z=5
  DOOR        at=(20,0)

  ROOM_PENT   polygon=((40,-10),(60,-6),(64,10),(52,16),(36,10))
              floor_z=0 ceil_z=8 name=boss_arena
  MARKER      x=50 y=0 z=1 name=boss_arena_entry

  RAMP_UP     from=(40,10) to=(50,10) dz=6
EBLOCK
```

### BNF

```
level     := "@grammar" NAME "v" NUMBER token*
token     := keyword param* | BLOCK token* EBLOCK | COMMENT
keyword   := ROOM_QUAD | ROOM_PENT | CORRIDOR_H | CORRIDOR_V
           | CORRIDOR_DIAG | RAMP_UP | RAMP_DOWN | DOOR | WINDOW
           | SPAWN | MARKER
param     := NAME '=' (NUMBER | '(' coords ')' | STRING)
coords    := coord (',' coord)*
coord     := '(' NUMBER ',' NUMBER (',' NUMBER)? ')'
```

---

## 3. Rasterizer Pipeline

```
token string → tokenizer (validates) → token[] → rasterizer → fr_dungeon_layout_t
```

### fr_dungeon_layout_t

```c
typedef struct {
    uint32_t version;
    struct {
        uint32_t room_count;       fr_room_def_t *rooms;
        uint32_t corridor_count;   fr_corridor_def_t *corridors;
        uint32_t opening_count;    fr_opening_def_t *openings;
        uint32_t ramp_count;       fr_ramp_def_t *ramps;
        uint32_t marker_count;     fr_marker_def_t *markers;
    } geometry;
    fr_vec3_t spawn_pos;
    fr_quat_t spawn_rot;
    struct {
        uint32_t node_count;       fr_nav_node_t *nodes;
        uint32_t edge_count;       fr_nav_edge_t *edges;
    } nav_graph;
    char grammar_name[64];
    uint32_t grammar_version;
    char architect_prompt[1024];
    char raw_token_string[8192];
} fr_dungeon_layout_t;
```

---

## 4. Serialization (Two Output Targets)

### 4a. JSON Level (existing engine format)
Convert `fr_dungeon_layout_t` → JSON for `edit_level_deserialize`:
- Rooms → `func_geo` entities with convex hull colliders
- Corridors → `func_geo` entities with extruded colliders
- Markers → `info_target` entities
- Spawn → `info_player_start`

### 4b. Critic Hook Data
`fr_critic_hooks_t`: spawn position, marker list, nav graph adjacency — grammar-agnostic.

---

## 5. Architect VLM

### Flow
```
1. Load grammar → get vlm_system_prompt_fragment
2. Build full system prompt:
   - Role: "You are a level architect for a dungeon-crawling RPG."
   - Full grammar reference (BNF + examples)
   - "Output ONLY valid tokens, no explanation, no markdown."
   - "Include SPAWN and at least N MARKER tokens."
3. Send prompt to VLM (via existing engine_settings LLM infrastructure)
4. Receive response → attempt to tokenize
5. If tokenize fails:
   - Append error to prompt: "Parse error: [error]. Correct and retry."
   - GOTO 3 (up to max_retries)
6. If tokenize succeeds → rasterize → return fr_dungeon_layout_t
```

### System Prompt Template
See `architect_prompt.c` — includes full token reference, constraints (no overlapping rooms, clearance ≥ 0, SPAWN required, ≥3 MARKERs with distinct names), and the user request.

---

## 6. Critic — Embodied Playtester

### 6a. NitroGen Agent (external process)
NitroGen is a vision-action foundation model trained on 40,000 hours of gameplay across 1,000+ games. It takes game frames as input and outputs gamepad actions. Integration via:
- **Shared memory ring buffer**: C engine writes rendered frames → NitroGen reads
- **Unix pipe for actions**: NitroGen writes JSON action → C engine reads and applies

### 6b. Engine Hook System
```c
typedef enum {
    FR_CRITIC_EVENT_DEATH,
    FR_CRITIC_EVENT_MARKER_HIT,
    FR_CRITIC_EVENT_FELL_OOB,
    FR_CRITIC_EVENT_TIMEOUT,
    FR_CRITIC_EVENT_STUCK,
} critic_event_type_t;
```

Hooks are registered before each playthrough. Events record: playthrough ID, frame, elapsed time, player position/velocity, marker name (for hits), damage source (for deaths).

### 6c. Critic Runtime
- Runs N playthroughs (default 10)
- Per-playthrough timeout (default 300s)
- Collects: death positions/times, marker reached status, distance traveled
- Computes: success rate, avg survival time, death heatmap, most lethal zone
- After each playthrough: sends screenshots to lightweight VLM for visual coherence critique

### 6d. Lightweight VLM (Visual Coherence)
- After each playthrough, screenshots at death points + marker locations
- Small VLM (Qwen2.5-VL-3B or Gemma-3-4B) answers: "Does this room look coherent? Glitches? Z-fighting? Missing textures? Impossible geometry?"
- Returns coherence score + issue list

---

## 7. File Layout

```
src/procgen/
├── procgen_types.h              # procgen_token_t, token types enum
├── procgen_layout.h             # fr_dungeon_layout_t, geometry defs
├── procgen_tokenize.h           # Tokenizer interface
├── procgen_tokenize.c           # Generic lexer
├── procgen_rasterize.c          # Rasterizer framework
├── procgen_serialize.c          # Layout → JSON
├── procgen_grammar_registry.c   # Multi-grammar registry
├── grammars/
│   └── grammar_blockout.c       # Blockout grammar (rooms, corridors, ramps)
├── architect/
│   ├── architect_config.h
│   ├── architect_run.c          # VLM pipeline + reprompting
│   └── architect_prompt.c       # System prompt builder
├── critic/
│   ├── critic_config.h
│   ├── critic_runtime.c         # Run loop
│   ├── critic_hooks.c           # Engine hook registration
│   ├── critic_summary.c         # Statistics
│   └── critic_vlm_visual.c      # Coherence VLM integration
└── nitrogen/
    ├── nitrogen_shm.c           # Shared memory ring buffer
    └── nitrogen_ipc.c           # Action pipe protocol

tools/
├── nitrogen_bridge.py           # NitroGen model wrapper
└── critic_visual_vlm.py         # Lightweight VLM coherence check

tests/procgen/
├── procgen_tokenize_tests.c
├── procgen_rasterize_tests.c
├── procgen_grammar_blockout_tests.c
├── procgen_serialize_tests.c
├── procgen_architect_tests.c
└── procgen_critic_tests.c
```

---

## 8. Implementation Phases

| Phase | Name | Summary |
|-------|------|---------|
| P0 | Types + Tokenizer | Token types, lexer, layout structs, tokenizer tests |
| P1 | Blockout Grammar | Tokenize + rasterize rooms, corridors, ramps, doors, markers, nav graph |
| P2 | Rasterizer + Serializer | Layout → JSON level + engine entities |
| P3 | Grammar Registry | Multi-grammar support, runtime selection |
| P4 | Architect VLM | System prompt builder, reprompting loop, LLM integration |
| P5 | Critic Hook System | Death/marker/stuck/fall event hooks |
| P6 | NitroGen IPC | Shared memory frame ring, action pipe, Python bridge |
| P7 | Critic Runtime | N playthroughs, event collection, summary statistics |
| P8 | Visual Coherence VLM | Lightweight VLM for screenshot critique |
| P9 | Integration + E2E Tests | Full pipeline: architect → rasterize → serialize → load → critic → report |

### Dependency Graph

```
P0 ──→ P1 ──→ P2
  │     │
  │     └──→ P3
  │           │
  ├───────────┴──→ P4
  │
  └──→ P5 ──→ P7 ←── P6
              │
              └── P8
                    │
                    └── P9
```

### TDD Strategy

Every phase follows RED-GREEN-REFACTOR:
1. **RED**: Write test file first — tests compile and run but fail
2. **GREEN**: Implement minimal code to pass tests
3. **REFACTOR**: Clean up, extract helpers, ensure existing tests still pass

Tests are standalone executables following the existing pattern in `tests/` (use `RUN()`/`ASSERT_TRUE()`/`ASSERT_EQ()` macros).

---

## 9. Key Design Decisions

1. **Grammars as compiled C code**: Not a runtime DSL — each grammar is a C module implementing `tokenize()` + `rasterize()`. Type safety, compile-time checking, no parser for grammar definitions needed. The token string format IS the DSL.

2. **Reuse existing LLM infrastructure**: Architect VLM uses the same `llm_base_url`/`llm_model` from `engine_settings` and the same HTTP/cost-tracking layer.

3. **NitroGen as external process**: Python + PyTorch required. Runs as subprocess with shared-memory IPC. Keeps C engine lightweight.

4. **Markers embedded in grammar tokens**: The architect outputs MARKER tokens with names. No separate spec sheet — markers ARE the level description. Critic validates them directly.

5. **Blockout grammar as reference**: Convex polytope rooms, axis-aligned/45°/30°-60° corridors, linear ramps. Simple to rasterize, complex enough for interesting dungeons.

6. **Grammar-agnostic critic**: Critic only needs `spawn_pos` + `markers[]` + `nav_graph`. Works with any future grammar.
