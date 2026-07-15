---
id: rpg-9ont
status: closed
deps: []
links: []
created: 2026-07-13T05:09:58Z
type: task
priority: 2
assignee: KMD
parent: rpg-w1qe
---
# texture_t abstraction (create/upload/bind, sRGB vs linear, mips, samplers)

Core-renderer GL texture abstraction (currently spec-only). Create from CPU pixels or file, choose internal format + sRGB-vs-linear, generate mips, set sampler params (filter/wrap/anisotropy), bind to a texture unit. Foundation for all PBR maps and the lightmap atlas.

## Design

src/renderer + include/ferrum/renderer. Load glGen/Bind/TexImage2D/TexParameteri/GenerateMipmap/ActiveTexture via gl_loader_t (no globals). Distinguish colour (sRGB: albedo/emissive) vs data (linear: normal/metal/rough/AO) textures.

