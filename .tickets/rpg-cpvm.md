---
id: rpg-cpvm
status: closed
deps: [rpg-fb3e]
links: []
created: 2026-02-26T04:27:43Z
type: task
priority: 2
assignee: KMD
parent: rpg-tiet
tags: [editor, assets, client]
---
# Client-side asset cache

Implement the client-side asset cache that avoids re-downloading assets.

READ FIRST: ref/editor_design.md §3.3 for cache design.

Requirements:
- Cache directory: ~/.ferrum_cache/assets/ mirroring server asset paths
- cache.json manifest: path → hash → local file mapping
- Before downloading, check cache for existing asset with matching hash
- Cache invalidation: server sends asset_changed event, client re-downloads if asset in use
- Cache size limit (configurable, default 512 MB), LRU eviction
- Thread-safe: downloads happen on a background thread, cache lookups on render thread

Files to create:
- src/editor/client/client_asset_cache.c
- include/ferrum/editor/client/client_asset_cache.h
- tests/editor/client_asset_cache_tests.c

