---
id: rpg-hrrd
status: open
deps: [rpg-p9zq]
links: [rpg-tqrt, rpg-q6fa, rpg-eabp, rpg-m3nv, rpg-2nxx, rpg-ebpv]
created: 2026-03-03T03:02:21Z
type: task
priority: 2
assignee: KMD
---
# Aegis IL: renderer script keys and entity_attrs extensions

Add all renderer-related SCRIPT_KEY_* constants to entity_attrs.h and ensure the Aegis VM set_field opcode handles them correctly. This is a prerequisite tracking ticket — individual keys are implemented by their phase tickets, but this ticket covers the schema design and VM validation.

New keys to add (allocated in block 15-28):
- SCRIPT_KEY_MESH (u32, key=15) — mesh_registry handle
- SCRIPT_KEY_RENDER_QUEUE (u32, key=16) — opaque/transparent/overlay
- SCRIPT_KEY_PARENT (u32, key=17) — parent entity ID for scene graph
- SCRIPT_KEY_BASE_COLOR (vec3, key=18) — runtime base color override
- SCRIPT_KEY_EMISSIVE (f32, key=19) — runtime emissive intensity
- SCRIPT_KEY_ALPHA (f32, key=20) — runtime alpha/opacity
- SCRIPT_KEY_ANIM_CLIP (str, key=21) — current animation clip name
- SCRIPT_KEY_ANIM_TIME (f32, key=22) — animation time (normalized 0-1)
- SCRIPT_KEY_ANIM_SPEED (f32, key=23) — playback speed multiplier
- SCRIPT_KEY_RAGDOLL (bool, key=24) — ragdoll toggle
- SCRIPT_KEY_LIGHT_COLOR (vec3, key=25) — runtime light color
- SCRIPT_KEY_LIGHT_INTENSITY (f32, key=26) — runtime intensity
- SCRIPT_KEY_LIGHT_RANGE (f32, key=27) — runtime range
- SCRIPT_KEY_CAST_SHADOW (bool, key=28) — shadow toggle

Deliverables:
- Define all keys in entity_attrs.h with types and documentation
- Ensure aegis_state_update_t validation accepts these key ranges
- Ensure set_field opcode in VM dispatch correctly packs vec3 (12 bytes) and str (variable) payloads for these keys
- Add IL assembler mnemonics for common patterns (e.g., 'set_color r0 r1' as alias for 'set_field r0 18 r1')
- Tests: VM round-trip for each key type (u32, f32, vec3, str, bool)

Depends on: rpg-p9zq (Phase 3 scripting must be at least partially landed for set_field to exist)

