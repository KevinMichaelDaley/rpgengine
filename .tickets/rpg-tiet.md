---
id: rpg-tiet
status: closed
deps: [rpg-wxom]
links: []
created: 2026-02-26T04:27:43Z
type: epic
priority: 1
assignee: KMD
tags: [editor, assets]
---
# Phase 2: Asset System

Epic for the asset management system. This phase adds asset registry, TCP asset downloads, client-side caching, tab-completion for asset paths, the browse command with #N references, material assignment, and entity cloning.

Before starting any subtask, read:
- ref/editor_spec.md §2.2 (asset registry), §2.3 (asset downloader)
- ref/editor_design.md §3 (asset download protocol), §9.5 (tab completion), §9.6 (browse caching)
- ref/editor_ux.md §6 (asset browser), §3.2 (tab completion), §3.3 (browse references)

All processes communicate over TCP and can run on separate machines.

