---
id: rpg-eabp
status: open
deps: [rpg-tqrt, rpg-sukf]
links: [rpg-tqrt, rpg-q6fa, rpg-m3nv, rpg-2nxx, rpg-ebpv, rpg-hrrd]
created: 2026-03-03T03:01:48Z
type: task
priority: 2
assignee: KMD
---
# Editor integration: lighting and shadow control (Phase 6)

Wire the lighting and shadow system into the editor with light entity types and IL-driven light control. See ref/renderer_spec.md Phase 6.

Deliverables:
- Add EDIT_ENTITY_TYPE_LIGHT to edit_entity.h
- New editor command 'light' to create/configure lights: light <type> [--color r g b] [--intensity f] [--range f] [--inner_cone f] [--outer_cone f] [--shadows] [--static]
- Type subcommands: 'light directional', 'light point', 'light spot'
- Editor 'light' command modifies existing light entity when entity_id provided
- Light entities appear in scene graph (position from scene node for point/spot, direction from rotation for directional/spot)
- Bridge callback on_light_spawn: registers light in light culling system
- Bridge callback on_light_update: updates light parameters, marks shadow map dirty if static light moves
- Static light shadow caching: bridge marks light is_static, caster pass only re-renders shadow map when invalidated
- Add SCRIPT_KEY_LIGHT_COLOR (vec3, key=25) for runtime light color
- Add SCRIPT_KEY_LIGHT_INTENSITY (f32, key=26) for runtime intensity
- Add SCRIPT_KEY_LIGHT_RANGE (f32, key=27) for runtime range (point/spot)
- Add SCRIPT_KEY_CAST_SHADOW (bool, key=28) for runtime shadow toggle
- IL light control: set_field with keys 25-28 for dynamic light scripting (flickering torches, day/night cycle, alarm lights)
- IL example: load_imm r1 26; clock r2; sin r3 r2; fmul r3 r3 r40; fadd r3 r3 r41; build_update r0; target_entity r0 r50; set_field r0 26 r3; push_update r0 (flickering intensity)
- SH probe bake command: 'probe_bake' triggers offline probe computation for current light setup, stores in probe grid texture
- Probe grid auto-placement: 'probe_auto' places probes on regular grid within scene bounds
- Replication: light entities sent to client with type + parameters in spawn message
- Tests for light spawn/configure, shadow invalidation, IL light animation, probe bake

Depends on: rpg-tqrt (animation integration for shadow-casting skinned meshes), rpg-sukf (SH probes implementation)

