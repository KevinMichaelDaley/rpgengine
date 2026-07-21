---
id: rpg-wcc4
status: open
deps: []
links: []
created: 2026-07-21T01:37:06Z
type: task
priority: 2
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, streaming]
---
# Compressed material textures (BC1/BC5/BC4) with pre-baked mips

Section 5.3 (VRAM + load time). texture_format.h:24-32 stops at RGB32F -- no BC path anywhere. Every material map is decoded on the GL thread, uploaded uncompressed (GL_RGB8 hits the driver swizzle slow path), then glGenerateMipmap per texture (client_scene_load.c:110-114, texture_upload.c:23-25). 4-8x VRAM + sampling bandwidth vs BC on every card; on shared-memory APUs wasted VRAM is wasted system RAM.
Fix: offline-compress (BC1 albedo/emissive, BC5 normal, BC4/BC1 orm) with pre-baked mip chains (DDS/KTX) via glCompressedTexImage2D; interim knob texture_quality that drops top mip(s) at load (works before BC lands). Also resource_loader.c: forces RGBA8 (:29-40), creates textures with GL_LINEAR min filter and NO mip chain (gpu_executor.c:77-78 -- mipgen loaded at :35, never called -> shimmer), and gpu_executor_drain (:128-152) pops until empty (uncapped uploads/frame).

## Acceptance Criteria

Material textures load as BC-compressed with pre-baked mips via glCompressedTexImage2D; texture_quality drops mips at load; the resource_loader path builds a mip chain and caps uploads/frame.

