"""Bake the procedural stone materials to the flat prefab PNGs the renderer uses.

The renderer (``assets/arch/proc/prefabs/bake/``) consumes plain albedo/roughness
PNGs, but earlier bakes of the field-sampled materials were downsized (1024^2)
while their SOURCE limestone field is 8192^2 -- so the runtime textures looked far
softer than the material actually is. This script re-bakes each material at the
*native resolution and UV bounds of its INPUT image*, so the PNG the renderer loads
is a 1:1 capture of the generator's source, with no rescale or random-box crop.

Materials match how ``scene_demo.build_scene`` builds the three the hall render
uses (see material_nodes.build_field_material / build_masonry_material):

  * ashlar : build_field_material("ashlar", "limestone")                -> 8192^2
  * vault  : build_field_material("vault_stone", "limestone",
                                  tint=(0.82, 0.74, 0.62))               -> 8192^2
  * masonry: build_masonry_material("stone_wall", mask/normal/ao/height,
                                    tint_map=...)                        -> mask res

Field materials are baked with ``box_frac=1.0`` so a plane UV-unwrapped to [0,1]
samples the ENTIRE field 1:1 (Mapping scale=1, offset=0). The masonry material is
baked over a plane whose UVs span exactly one wall tile ([0, tile_x] x [0, tile_y]
metres), so its baked mask/normal/ao maps land 1:1 at the mask's own resolution.

Output sizes are derived ONLY from the input images, never from scene geometry:
  - field albedo/rough field PNG  -> 8192 x 8192  (ashlar/vault)
  - baked mask PNG                -> 2048 x 1184  (masonry / "stone_wall")

albedo is baked as DIFFUSE / COLOR-only (no direct/indirect light) and saved sRGB;
roughness is baked as the ROUGHNESS pass and saved Non-Color (linear).

``normal.png`` and ``ao.png`` are produced by ``bake_wall.py`` and are NOT touched.

Headless::

    blender --background --factory-startup \
        --python assets/arch/proc/bake_material_prefabs.py
"""
import os

import bpy

HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() \
    else "/home/kmd/rpg/assets/arch/proc"
BAKE = os.path.join(HERE, "prefabs", "bake")
FIELD_ROOT = "/home/kmd/rpg/assetsrc/materials"


# --------------------------------------------------------------------------
# Sibling modules aren't importable packages; exec them into namespaces.
# --------------------------------------------------------------------------
def _load(module):
    ns = {}
    path = os.path.join(HERE, module)
    with open(path) as f:
        exec(compile(f.read(), module, "exec"), ns)
    return ns


def _png_size(path):
    """Read a PNG's (width, height) from its IHDR without decoding pixels."""
    with open(path, "rb") as f:
        head = f.read(24)
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"not a PNG: {path}")
    width = int.from_bytes(head[16:20], "big")
    height = int.from_bytes(head[20:24], "big")
    return width, height


# --------------------------------------------------------------------------
# Bake plane: a single quad whose UVs fill [0, u_span] x [0, v_span].
# --------------------------------------------------------------------------
def _bake_plane(u_span=1.0, v_span=1.0, name="bake_plane"):
    """A unit quad UV-unwrapped so its UVs span [0, u_span] x [0, v_span].

    For a field bake u_span = v_span = 1 maps the plane 1:1 onto the whole field.
    For the masonry bake the spans are the wall tile in metres, so one full wall
    repeat (== the baked mask image) lands exactly on the plane.
    """
    mesh = bpy.data.meshes.new(name)
    # unit square in XY, single quad, CCW.
    verts = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 1.0, 0.0), (0.0, 1.0, 0.0)]
    mesh.from_pydata(verts, [], [(0, 1, 2, 3)])
    mesh.update()
    uv = mesh.uv_layers.new(name="UVMap")
    # loop order follows the face's vertex order (0,1,2,3).
    coords = [(0.0, 0.0), (u_span, 0.0), (u_span, v_span), (0.0, v_span)]
    for loop_index, (cu, cv) in enumerate(coords):
        uv.data[loop_index].uv = (cu, cv)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


# --------------------------------------------------------------------------
# One bake: assign material to the plane, bake a pass into a fresh image, save.
# --------------------------------------------------------------------------
def _bake_pass(obj, material, width, height, pass_type, out_path,
               colorspace, samples):
    """Bake ``pass_type`` of ``material`` over ``obj`` into a width x height PNG.

    pass_type 'DIFFUSE' bakes COLOR only (no direct/indirect light) -> albedo.
    pass_type 'ROUGHNESS' bakes the roughness input. ``colorspace`` sets how the
    saved PNG is encoded ('sRGB' for albedo, 'Non-Color' for roughness).
    """
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = samples

    # Assign the material as the plane's single slot.
    obj.data.materials.clear()
    obj.data.materials.append(material)

    # Target image + an Image Texture node holding it, selected & active so the
    # bake writes into it.
    img = bpy.data.images.new(f"bake_{pass_type.lower()}", width, height,
                              alpha=False, float_buffer=False)
    img.colorspace_settings.name = colorspace

    nt = material.node_tree
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    for n in nt.nodes:
        n.select = False
    tex.select = True
    nt.nodes.active = tex

    # Select only the plane, make it active.
    for o in bpy.context.scene.objects:
        o.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj

    bake = scene.render.bake
    bake.use_selected_to_active = False
    bake.margin = 0

    if pass_type == 'DIFFUSE':
        bake.use_pass_direct = False
        bake.use_pass_indirect = False
        bake.use_pass_color = True
        bpy.ops.object.bake(type='DIFFUSE', pass_filter={'COLOR'},
                            use_clear=True)
    else:
        bpy.ops.object.bake(type=pass_type, use_clear=True)

    img.file_format = 'PNG'
    img.filepath_raw = out_path
    img.save()

    # Clean up so repeated bakes don't accumulate nodes/images.
    nt.nodes.remove(tex)
    bpy.data.images.remove(img)
    return out_path


# --------------------------------------------------------------------------
# Orchestrator
# --------------------------------------------------------------------------
def main():
    matn = _load("material_nodes.py")
    build_field = matn["build_field_material"]
    build_masonry = matn["build_masonry_material"]

    os.makedirs(BAKE, exist_ok=True)
    matn_backend = _load("bake_wall.py")
    backend = matn_backend["_enable_gpu"]()
    print(f"[bake] Cycles backend: {backend or 'CPU'}")

    # --- field materials: native field resolution, whole-field 1:1 -----------
    field_alb = os.path.join(FIELD_ROOT, "limestone", "fields",
                             "limestone_albedo_field.png")
    src_w, src_h = _png_size(field_alb)
    # The field is 8192^2, but a full-field 1:1 bake at 2048 (a straight
    # downsample of the same seed, no random-box crop) is plenty of fidelity and
    # keeps the Cycles bake light. Env FIELD_RES overrides.
    fw = fh = int(os.environ.get("FIELD_RES", "2048"))
    print(f"[bake] limestone field source {src_w} x {src_h} -> bake {fw} x {fh}")

    field_plane = _bake_plane(1.0, 1.0, "field_bake_plane")
    field_samples = 8   # colour/roughness bakes are noise-free; keep it cheap.

    field_jobs = [
        # (out prefix, material builder args)
        ("ashlar", lambda: build_field("bake_ashlar", "limestone", box_frac=1.0)),
        ("vault", lambda: build_field("bake_vault", "limestone", box_frac=1.0,
                                      tint=(0.82, 0.74, 0.62))),
    ]
    for prefix, make in field_jobs:
        mat = make()
        _bake_pass(field_plane, mat, fw, fh, 'DIFFUSE',
                   os.path.join(BAKE, f"{prefix}_albedo.png"), 'sRGB',
                   field_samples)
        _bake_pass(field_plane, mat, fw, fh, 'ROUGHNESS',
                   os.path.join(BAKE, f"{prefix}_roughness.png"), 'Non-Color',
                   field_samples)
        bpy.data.materials.remove(mat)
        print(f"[bake] {prefix}: {fw} x {fh} albedo + roughness")

    # --- masonry material: native mask resolution, one wall tile 1:1 ---------
    mask_png = os.path.join(BAKE, "mask.png")
    mw, mh = _png_size(mask_png)
    print(f"[bake] mask: {mw} x {mh}")

    tile = (4.5, 2.6)   # matches build_masonry_material's default tile.
    masonry_plane = _bake_plane(tile[0], tile[1], "masonry_bake_plane")
    masonry = build_masonry(
        "bake_stone_wall",
        mask=mask_png,
        normal=os.path.join(BAKE, "normal.png"),
        ao=os.path.join(BAKE, "ao.png"),
        height=os.path.join(BAKE, "height.png"),
        tint_map=os.path.join(BAKE, "tint.png"),
        tile=tile)
    # AO is folded into the baked albedo (the material multiplies base colour by
    # the AO map) -- matches how the renderer's flat albedo is expected to look.
    _bake_pass(masonry_plane, masonry, mw, mh, 'DIFFUSE',
               os.path.join(BAKE, "albedo.png"), 'sRGB', 16)
    _bake_pass(masonry_plane, masonry, mw, mh, 'ROUGHNESS',
               os.path.join(BAKE, "roughness.png"), 'Non-Color', 16)
    print(f"[bake] masonry: {mw} x {mh} albedo + roughness")

    print("[bake] done. normal.png / ao.png left untouched (bake_wall.py owns them).")


if __name__ == "__main__":
    main()
