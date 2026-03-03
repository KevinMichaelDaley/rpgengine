---
id: rpg-q6fa
status: open
deps: [rpg-ebpv, rpg-ifb2]
links: [rpg-tqrt, rpg-eabp, rpg-m3nv, rpg-2nxx, rpg-ebpv, rpg-hrrd]
created: 2026-03-03T03:01:01Z
type: task
priority: 2
assignee: KMD
---
# Editor integration: material system and texture binding (Phase 4)

Wire the material system into the editor, extending existing material commands and adding IL hooks. See ref/renderer_spec.md Phase 4.

Deliverables:
- Extend existing 'material' command to resolve texture paths via asset_registry and load into render_material_t in material_registry
- Bridge callback on_material_change(entity_id, slot, path): loads texture, creates/updates render_material_t entry, assigns to entity
- Material assignment flows: editor 'material set' command -> asset_registry lookup -> texture load -> material_registry insert -> entity binding
- Material defaults: newly spawned entities get a default unlit material with base_color from entity color attribute
- Shader permutation auto-selection: material_registry inspects which texture slots are populated and selects matching permutation from permutation_cache
- Extend spawn message: include material_handle so client can look up from its local material_registry
- Add SCRIPT_KEY_BASE_COLOR (vec3, key=18) to entity_attrs.h for runtime color changes
- Add SCRIPT_KEY_EMISSIVE (f32, key=19) for runtime emissive intensity
- Add SCRIPT_KEY_ALPHA (f32, key=20) for runtime alpha/opacity control
- IL support: set_field with keys 18/19/20 for scripts to animate material properties (pulsing glow, fade effects, color transitions)
- IL example snippet: load_imm r1 18; clock r2; sin r3 r2; vec3_pack r4 r3 r3 r3; build_update r0; target_entity r0 r50; set_field r0 18 r4; push_update r0 (pulsing color)
- Editor command 'shader' to override permutation: shader <entity_id> <permutation_name> (for debug/testing)
- Tests for material load/assign, permutation selection, runtime color animation via script

Depends on: rpg-ebpv (scene graph hierarchy for material inheritance), rpg-ifb2 (shader permutations)

