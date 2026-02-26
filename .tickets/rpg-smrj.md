---
id: rpg-smrj
status: open
deps: []
links: []
created: 2026-02-26T04:30:48Z
type: task
priority: 3
assignee: KMD
parent: rpg-l5le
tags: [editor, advanced, server]
---
# Hot-reload for scripts and assets (inotify)

Implement file-watching and hot-reload for scripts and assets.

READ FIRST: ref/editor_design.md §1 module layout (asset_watch.c).

Requirements:
- inotify-based file watcher on asset directories
- When a script file changes: re-execute it (if it was loaded with 'run')
- When an asset file changes: notify connected clients (asset_changed event), client re-downloads
- File watcher runs on I/O thread (integrates with epoll loop)
- Debounce: ignore rapid successive changes (wait 100ms after last change)
- Only watch files that have been accessed during this session

Files to create:
- src/editor/assets/edit_asset_watch.c
- include/ferrum/editor/edit_asset_watch.h
- tests/editor/edit_asset_watch_tests.c

