---
id: rust-rpg-1eu
status: open
deps: [rust-rpg-6he]
links: []
created: 2026-01-17T18:02:51.289735975-08:00
type: epic
priority: 2
---
# P_018 — Asset Cooking, Hot-Reload & Virtual Filesystem

## P_018 — Asset Cooking, Hot-Reload & Virtual Filesystem

### Design Intent
Add a build-time cooking pipeline with hashed dependencies, runtime hot-reload, and a virtual filesystem to abstract asset locations.

### Specification
- Cooked cache with content hashes; dependency graph.
- VFS mounts; path resolution rules.
- Hot-reload with thread-safe staging and fallback.

### Implementation Steps
1. Cook pipeline stubs and cache index.
2. VFS mount table; path resolution.
3. Hot-reload watchers; staging + swap.

### Architectural Considerations
- Deterministic hashing; safe fallbacks on failure.

### Unit/Regression/Cumulative Tests
- Cache hit/miss; dependency invalidation.
- Hot-reload correctness under streaming.

---



