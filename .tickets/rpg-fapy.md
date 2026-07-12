---
id: rpg-fapy
status: closed
deps: []
links: [rpg-droh]
created: 2026-07-10T09:01:33Z
type: task
priority: 1
assignee: KMD
parent: rpg-lbky
tags: [arch, materials, blender, uv]
---
# Pack UV islands into [0,1] at end of every UV-map function

Every mesh generator's UV finalize (assets/arch/proc/*.py `_finalize_uvs`, used
by arch.py / column.py / vault.py, and any future generators) must, at the END
of unwrapping, PACK the UV islands into the [0,1] bounds and scale the whole UV
map to fill [0,1].

- Use Blender's built-in operator `bpy.ops.uv.pack_islands` (rotate + scale to
  UDIM) — do NOT reimplement packing. `scale=True` fits/fills [0,1].
- This replaces the old absolute metre-scale (UV_SCALE=1) density normalization
  whose UVs exceed [0,1] and make the material TILE. With islands packed to
  [0,1], the object maps its whole surface into one box of the material field,
  so a generated material does NOT tile/repeat across the surface.
- Consequence for the material graph (rpg-lbky): build_field_material must map
  [0,1] UVs -> a random BOX (fraction) of the field, not scale by metres. Update
  the field-layer mapping accordingly (box size = feature-scale param, random
  offset per object, CLAMP so no repeat).
- Apply to all generators and re-verify a few objects (column/arch/dome) show a
  single non-tiling material box.


## Notes

**2026-07-10T09:22:35Z**

Done: _pack_islands added to arch.py/column.py/vault.py _finalize_uvs (bpy.ops.uv.pack_islands, UV-sync, scale-to-fill after density normalization); build_dome re-packs after interior-lens. Verified dome/dome+lens/barrel/column/doorway all pack to [0,1]. material_nodes.build_field_material updated to box-sample the packed [0,1] UVs. Committed 251cec64.
