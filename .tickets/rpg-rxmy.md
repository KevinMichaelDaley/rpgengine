---
id: rpg-rxmy
status: open
deps: [rpg-jyx3]
links: []
created: 2026-03-01T06:47:53Z
type: task
priority: 1
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, security]
---
# Exploit pattern database: multi-graph detection of known attacks

Pre-execution scanner that checks script source against a multi-graph database of known exploit patterns.

## Architecture
- Each exploit pattern is a directed multi-graph of operation nodes
- Nodes represent operation types (CALL, INDEX, SETGLOBAL, CONCAT, LEN, LOOP, etc)
- Multiple edge types per node pair: data flow, control flow, temporal, semantic similarity
- Patterns are invariant to variable names, ordering, and specific instruction encoding
- Matching uses subgraph isomorphism with wildcard nodes

## Database
- Built-in patterns for known scripting language attacks:
  1. String bomb (string.rep with computed large size)
  2. Table bomb (loop creating huge nested tables)
  3. Pattern DoS (pathological string.find/gmatch patterns)
  4. Stack overflow (deep recursive call chains without base case detection)
  5. Metatable escape (setmetatable chains if somehow bypassing sandbox)
  6. Type confusion (repeated tostring/tonumber casting chains)
  7. GC abuse (collectgarbage with specific args)
  8. Coroutine escape (coroutine.wrap + pcall to break out)
  9. Memory probing (systematic string.rep to probe allocator limits)

## Scan API
- script_exploit_db_init(db) — initialize
- script_exploit_db_load_defaults(db) — load built-in patterns
- script_exploit_scan(db, source, result) — scan source code
- Returns: blocked (bool), matched pattern name, confidence (0-1), reason

## Graph building
- Tokenize script source into operation sequence
- Build adjacency lists with typed edges
- Compare against each pattern using BFS-based subgraph matching

## Files
- include/ferrum/editor/edit_script_exploit.h (types: script_exploit_db_t, script_scan_result_t)
- src/editor/script/edit_script_exploit_db.c (db lifecycle)
- src/editor/script/edit_script_exploit_scan.c (scan + graph matching)
- src/editor/script/edit_script_exploit_graph.c (build graph from source)
- src/editor/script/edit_script_exploit_patterns.c (built-in pattern definitions)
- tests/editor/edit_script_exploit_tests.c

## Tests
- DB init and destroy
- Load defaults populates patterns
- Clean script passes scan
- String bomb detected (string.rep(x, 2^20))
- Table bomb detected (nested loop creating tables)
- Pattern DoS detected (pathological regex)
- Recursive bomb detected
- Coroutine escape attempt detected
- GC abuse detected
- Multiple patterns in one script
- Confidence threshold filtering
- Empty script passes
- Normal string.rep usage passes (not flagged as bomb)

