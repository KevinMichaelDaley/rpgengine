"""Export a Blender collection to the Talarium engine.

Produces, under an output directory:
  * ``meshes/<name>.fvma``       -- every mesh, LOCAL space, via the .fvma writer.
  * ``materials/<mat>.mat.json`` -- one per unique material, in the engine's PBR
        contract (tint / roughness / ao / normal-scale + baked map references),
        with the node materials Cycles-BAKED to albedo/normal/roughness/ao PNGs.
  * ``scene.json``               -- the manifest: for every object its mesh file,
        world POSITION + ORIENTATION (engine Y-up) + scale, and its material list
        (one name per polygroup / material slot).

Coordinates are converted Blender Z-up -> engine Y-up: position (x,y,z)->(x,z,-y)
and orientation quaternion (x,y,z,w)->(x,z,-y,w) (matching scripts/export_fvma.py).

Run inside Blender (the scene must be loaded)::

    exec(open("scripts/export_scene.py").read(), {"OUT": "/path/out",
         "COLLECTION": "great_hall"})
or headless with the same globals via --python-expr.
"""
import json
import math
import os

import bpy
from mathutils import Vector

_HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() \
    else "/home/kmd/rpg/scripts"

# Reuse the .fvma mesh writer (single source of truth for the binary format).
_FV = {}
with open(os.path.join(_HERE, "export_fvma.py")) as _f:
    exec(compile(_f.read(), "export_fvma.py", "exec"), _FV)
_gather_mesh_data = _FV["_gather_mesh_data"]
_write_fvma = _FV["_write_fvma"]

# Physical tile size (metres one map repeat spans) per tiling material. Solid
# materials (not listed) export as flat tint + roughness with no maps.
# Physical tile (metres one map repeat spans), per axis (x, y). MUST match the
# material's own `tile=` so the baked square image + per-axis uv_scale reproduce
# the in-Blender pattern at the right size/aspect. The masonry wall/reveal tiles
# are NON-SQUARE (brick courses are shorter than they are wide), which a single
# square tile got wrong (bricks came out mis-sized). A scalar still works (square).
DEFAULT_TILES = {
    "great_hall_stone_wall": (4.5, 2.6),   # = build_masonry_material default tile
    "great_hall_floor_stone": (4.2, 4.2),
    "great_hall_reveal_weave": (1.2, 1.0),
    # Timber uses beam-space UVs directly (tile 1 -> uv_scale 1); the limestone
    # roof / marble dais are field materials whose world-cube UV scale (0.2 / 0.5)
    # times uv_scale=1/tile reproduces their in-Blender repeat.
    "great_hall_timber": (1.0, 1.0),
    "great_hall_roof_limestone": (2.0, 2.0),
    "great_hall_dais_marble": (2.0, 2.0),
}


#: target lightmap luxel density -- at most 1 luxel per this many metres (5 cm).
LUXEL_METRES = 0.05
LMRES_CAP = 1024


def _read_lighting():
    """Capture the scene's sun (travel direction in engine Y-up + radiance =
    colour*energy) and constant sky radiance (world background colour*strength),
    so the bake matches what the .blend currently shows."""
    import mathutils
    sun = next((o for o in bpy.data.objects
                if o.type == 'LIGHT' and o.data.type == 'SUN'), None)
    if sun is not None:
        fwd = (sun.matrix_world.to_3x3()
               @ mathutils.Vector((0, 0, -1))).normalized()
        d = [float(fwd.x), float(fwd.z), float(-fwd.y)]      # travel dir, Y-up
        e = float(sun.data.energy)
        c = [float(sun.data.color[i]) * e for i in range(3)]
    else:
        d, c = [0.42, -0.5, 0.76], [4.0, 4.0, 4.0]
    w = bpy.context.scene.world
    try:
        bg = w.node_tree.nodes["Background"]
        s = float(bg.inputs[1].default_value)
        sky = [float(bg.inputs[0].default_value[i]) * s for i in range(3)]
    except (AttributeError, KeyError, TypeError):
        sky = [0.78, 0.94, 1.28]
    return {"sun_dir": d, "sun_color": c, "sky_color": sky}


def _scene_light(o):
    """Extract a scene-descriptor light dict from a Blender LIGHT object (engine
    Y-up conversion here; the pure ::_light_record does the schema mapping)."""
    import mathutils
    L = o.data
    kind = _LIGHT_KIND.get(L.type, "point")
    fwd = (o.matrix_world.to_3x3() @ mathutils.Vector((0, 0, -1))).normalized()
    direction = [float(fwd.x), float(fwd.z), float(-fwd.y)]  # travel dir, Y-up
    rng = (float(getattr(L, "cutoff_distance", 0.0))
           if getattr(L, "use_custom_distance", False) else 0.0)
    return _light_record(
        kind, o.name,
        _engine_pos(o.matrix_world.translation), direction,
        [float(L.color[0]), float(L.color[1]), float(L.color[2])],
        float(L.energy), rng=rng,
        radius=float(getattr(L, "shadow_soft_size", 0.0)),
        spot_size_rad=float(getattr(L, "spot_size", 0.0)),
        spot_blend=float(getattr(L, "spot_blend", 0.0)),
        use_shadow=bool(getattr(L, "use_shadow", True)))


def _export_lights():
    """Every light in the scene -> descriptor light list. The exporter emits ALL
    lights (incl. the sun) so the descriptor is the single source of level
    lighting -- nothing is hand-authored or hardcoded in the runtime."""
    return [_scene_light(o) for o in bpy.context.scene.objects
            if o.type == 'LIGHT']


def _object_collider(o):
    """A streamed physics collider for a DYNAMIC-marked object, or None. Static
    level geometry (the great hall) is unmarked and gets no collider -- collision
    is opt-in per object via the ``ferrum_dynamic`` marking, the SAME property the
    object/bake path reads (a generator sets it once and both consumers agree).
    Non-armature dynamic bodies carry a primitive shape (``ferrum_obj_shape``);
    armature-driven bodies get their bone-keyed colliders from the .fskel."""
    if not bool(o.get("ferrum_dynamic", False)):
        return None
    shape = str(o.get("ferrum_obj_shape", "box"))
    kind = _COLLIDER_KIND.get(shape, "box")
    _loc, rot_q, _scale = o.matrix_world.decompose()
    dim = o.dimensions
    # Centre the proxy on the world-space BOUNDING BOX, not the object origin.
    # A generator that emits world-space vertices leaves the object transform at
    # identity (great_hall's cloth banner does), so the origin is (0,0,0) while
    # the geometry sits metres away -- the proxy would land in empty space. The
    # bbox centre is correct under both conventions, and matches o.dimensions,
    # which is already the bbox size.
    centre = o.matrix_world @ (sum((Vector(c) for c in o.bound_box),
                                   Vector()) / 8.0)
    rec = {"kind": kind, "name": o.name, "static": False,
           "position": _engine_pos(centre), "rotation": _engine_quat(rot_q)}
    if kind == "box":
        rec["half_extents"] = [float(dim.x) * 0.5, float(dim.z) * 0.5,
                               float(dim.y) * 0.5]
    elif kind == "sphere":
        rec["radius"] = float(max(dim)) * 0.5
    elif kind == "capsule":
        rec["radius"] = float(o.get("ferrum_capsule_radius",
                                    max(float(dim.x), float(dim.y)) * 0.5))
        rec["half_height"] = float(o.get("ferrum_capsule_height",
                                         float(dim.z) * 0.5))
    elif kind in ("convex", "mesh"):
        rec["mesh"] = os.path.join("meshes", o.name + ".fvma")
    return rec


def _export_colliders(col):
    """Colliders for every DYNAMIC-marked mesh object in the collection (empty for
    an all-static level like the great hall)."""
    out = []
    for o in col.objects:
        if o.type != 'MESH':
            continue
        c = _object_collider(o)
        if c is not None:
            out.append(c)
    return out


def _lmres_for(obj):
    """Per-mesh lightmap atlas-rect size for the target luxel density (1 luxel per
    LUXEL_METRES at most): the mesh's largest world dimension / LUXEL_METRES,
    clamped so tiny props still get a few texels and huge ones stay bounded."""
    dim = max(float(d) for d in obj.dimensions) if obj.dimensions else 0.0
    return max(8, min(LMRES_CAP, int(round(dim / LUXEL_METRES))))


def _engine_pos(v):
    """Blender Z-up location -> engine Y-up (x, z, -y)."""
    return [float(v.x), float(v.z), float(-v.y)]


def _engine_quat(q):
    """Blender orientation quaternion -> engine Y-up. The basis change is a -90
    deg rotation about X, so the axis (imaginary) part transforms like a vector
    (x, y, z)->(x, z, -y) and the scalar w is unchanged."""
    return [float(q.x), float(q.z), float(-q.y), float(q.w)]


# --------------------------------------------------------------------------
# Descriptor section builders (pure -- no bpy -- so they can be unit-tested)
# --------------------------------------------------------------------------

#: Blender light data-type -> scene-descriptor light kind.
_LIGHT_KIND = {"SUN": "directional", "POINT": "point",
               "SPOT": "spot", "AREA": "area"}


def _material_desc(rec):
    """Map an ``export_material`` record to a scene-descriptor material object
    (the schema scene_desc_material_t parses). Baked map paths (albedo/normal/
    roughness/ao) become texture references; scalar PBR params pass through. Pure:
    takes/returns plain dicts, so it is unit-testable without Blender."""
    maps = rec.get("maps", {})
    d = {
        "name": rec["name"],
        "tint": list(rec.get("tint", [1.0, 1.0, 1.0])),
        "metalness": float(rec.get("metalness", 0.0)),
        "roughness_min": float(rec.get("roughness_min", 0.0)),
        "roughness_max": float(rec.get("roughness_max", 1.0)),
        "normal_scale": float(rec.get("normal_scale", 1.0)),
        "ao_strength": float(rec.get("ao_strength", 1.0)),
        "uv_scale": list(rec.get("uv_scale", [1.0, 1.0])),
    }
    # Descriptor texture keys (see scene_desc_parse_objects.c material parser).
    for ch in ("albedo", "normal", "roughness", "ao"):
        if maps.get(ch):
            d[ch] = maps[ch]
    return d


def _light_record(kind, name, position, direction, color, energy,
                  rng=0.0, radius=0.0, spot_size_rad=0.0, spot_blend=0.0,
                  use_shadow=True):
    """Build a scene-descriptor light dict from already-converted (engine Y-up)
    values. The sun (a directional light) is flagged baked+dynamic_indirect+shadow
    so it folds into the offline lightmap AND is gathered by the runtime SDF-probe
    GI; other lights are realtime (+shadow). Spot cones (Blender full-angle
    radians + blend) become inner/outer DEGREES the descriptor stores as cosines.
    Pure -- no bpy -- so it is unit-testable."""
    rec = {"kind": kind, "name": name,
           "position": [float(c) for c in position],
           "direction": [float(c) for c in direction],
           "color": [float(c) for c in color],
           "intensity": float(energy)}
    if kind in ("point", "spot"):
        rec["range"] = float(rng)
        rec["radius"] = float(radius)
    if kind == "spot":
        outer_deg = math.degrees(spot_size_rad) * 0.5
        rec["cone_outer_deg"] = outer_deg
        rec["cone_inner_deg"] = outer_deg * (1.0 - float(spot_blend))
    if kind == "directional":
        # The sun: baked into the lightmap, opted into dynamic probe GI, shadowing.
        rec["flags"] = ["baked", "dynamic_indirect", "shadow"]
    else:
        # Dynamic punctual lights: realtime direct + gathered by the SDF-probe GI
        # (dynamic_indirect + probe_gi) for indirect bounce, like the hall demo's
        # lantern/sconces. They are NOT baked (no re-bake when they move).
        rec["flags"] = (["realtime", "dynamic_indirect", "probe_gi"] +
                        (["shadow"] if use_shadow else []))
    return rec


#: Talarium object collider shape -> scene-descriptor collider kind.
_COLLIDER_KIND = {"box": "box", "sphere": "sphere", "capsule": "capsule",
                  "convex_hull": "convex", "convex": "convex", "mesh": "mesh",
                  "point": "point"}


# --------------------------------------------------------------------------
# Mesh export
# --------------------------------------------------------------------------
def export_mesh(obj, out_dir):
    """Write ``obj`` to ``meshes/<name>.fvma`` (LOCAL space) and return the
    relative path. Normals/tangents/UVs on; no bones (static prop)."""
    # calc_tangents() aborts on n-gons, so triangulate first: a temporary
    # Triangulate modifier makes the EVALUATED mesh all tris (tangents then
    # compute cleanly) without altering the source mesh.
    tri = obj.modifiers.new("_export_tri", 'TRIANGULATE')
    tri.quad_method = 'BEAUTY'
    tri.ngon_method = 'BEAUTY'
    try:
        data = _gather_mesh_data(obj, export_normals=True, export_tangents=True,
                                 export_uvs=True, export_colors=False,
                                 export_bones=False)
    finally:
        obj.modifiers.remove(tri)
    rel = os.path.join("meshes", obj.name + ".fvma")
    path = os.path.join(out_dir, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    _write_fvma(path, data)
    return rel, data["vertex_count"], data["index_count"] // 3


# --------------------------------------------------------------------------
# Material bake
# --------------------------------------------------------------------------
def _bake_plane():
    """A unit grid whose UVs span exactly [0,1] x [0,1] so the whole plane fills
    the [0,1] bake image (Cycles only rasterises UVs in [0,1] into the target,
    so a [0,tile] UV would capture just a FRAGMENT of the map). The caller scales
    the material's Mapping nodes so ONE full map repeat lands on this [0,1]."""
    import bmesh
    me = bpy.data.meshes.new("_bake_tmp")
    bm = bmesh.new()
    n = 2
    vs = [[bm.verts.new((i / n, j / n, 0.0))
           for i in range(n + 1)] for j in range(n + 1)]
    uvl = bm.loops.layers.uv.new("UVMap")
    for j in range(n):
        for i in range(n):
            f = bm.faces.new((vs[j][i], vs[j][i + 1], vs[j + 1][i + 1],
                              vs[j + 1][i]))
            for lp in f.loops:
                lp[uvl].uv = (lp.vert.co.x, lp.vert.co.y)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new("_bake_tmp", me)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def _bake_pass(obj, res, bake_type, non_color, **kw):
    """Bake one pass of ``obj``'s active material into a fresh image; return it."""
    img = bpy.data.images.new("_bake", res, res, alpha=False,
                              float_buffer=False,
                              is_data=non_color)
    mat = obj.data.materials[0]
    nt = mat.node_tree
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    # The bake target is the SELECTED + ACTIVE image-texture node; without the
    # selection the bake writes nothing and the image stays black.
    for n in nt.nodes:
        n.select = False
    tex.select = True
    nt.nodes.active = tex
    for o in bpy.context.selected_objects:
        o.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.samples = kw.pop("samples", 8)
    # For DIFFUSE (albedo) isolate the COLOR pass -- otherwise it bakes the LIT
    # diffuse (direct+indirect) and, with no lights, comes out black.
    if bake_type == 'DIFFUSE':
        sc.render.bake.use_pass_direct = False
        sc.render.bake.use_pass_indirect = False
        sc.render.bake.use_pass_color = True
    bpy.ops.object.bake(type=bake_type, **kw)
    nt.nodes.remove(tex)
    return img


def _save_img(img, path):
    # NB: do NOT touch img.colorspace_settings here -- reassigning it after the
    # bake invalidates/clears the pixel buffer and writes a BLACK PNG. The
    # colorspace is already set correctly at creation via the is_data flag.
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)


def _principled(mat):
    """The Principled BSDF of a material, or None."""
    if not mat.use_nodes:
        return None
    for n in mat.node_tree.nodes:
        if n.type == 'BSDF_PRINCIPLED':
            return n
    return None


def export_material(mat, out_dir, tiles, res, bake=True):
    """Write ``materials/<mat>.mat.json`` (engine PBR contract). Tiling materials
    (in ``tiles``) are Cycles-baked to albedo/normal/roughness/ao PNGs; solid
    materials export flat base-colour tint + roughness with no maps.

    ``bake`` False skips the (slow) Cycles bake and instead references the map PNGs
    already on disk from a prior bake -- for iterating on geometry/descriptor
    without re-baking. The record + uv_scale are still emitted."""
    name = mat.name
    mdir = os.path.join(out_dir, "materials", name)
    rec = {"name": name, "tint": [1.0, 1.0, 1.0], "metalness": 0.0,
           "roughness_min": 0.0, "roughness_max": 1.0, "ao_strength": 1.0,
           "normal_scale": 1.0, "uv_scale": [1.0, 1.0], "maps": {}}

    if name not in tiles:
        # Solid material: pull flat tint + roughness straight off the Principled.
        p = _principled(mat)
        if p:
            bc = p.inputs["Base Color"].default_value
            rec["tint"] = [float(bc[0]), float(bc[1]), float(bc[2])]
            r = p.inputs["Roughness"].default_value
            rec["roughness_min"] = rec["roughness_max"] = float(r)
        _write_json(os.path.join(out_dir, "materials", name + ".mat.json"), rec)
        return rec

    # Tiling material: bake one FULL repeat. Tile is per-axis (x, y); scalar=square.
    t = tiles[name]
    tx, ty = (float(t), float(t)) if isinstance(t, (int, float)) else (float(t[0]), float(t[1]))
    os.makedirs(mdir, exist_ok=True)

    if not bake:
        # Reuse the maps already on disk (no Cycles bake); still emit the record.
        for ch in ("albedo", "roughness", "normal"):
            rel = os.path.join("materials", name, ch + ".png")
            if os.path.exists(os.path.join(out_dir, rel)):
                rec["maps"][ch] = rel
        rec["uv_scale"] = [1.0 / tx, 1.0 / ty]
        _write_json(os.path.join(out_dir, "materials", name + ".mat.json"), rec)
        return rec
    plane = _bake_plane()
    plane.data.materials.append(mat)
    # Scale every Mapping node by the tile so a mapping whose repeat == tile lands
    # exactly one full repeat on the [0,1] bake UV; other mappings (e.g. the finer
    # aperiodic field) keep their RELATIVE density (their_repeat/tile). Restored
    # after, so the live material is untouched.
    saved = []
    for nd in mat.node_tree.nodes:
        if nd.type == 'MAPPING':
            s = nd.inputs["Scale"].default_value
            saved.append((nd, (s[0], s[1], s[2])))
            s[0] *= tx
            s[1] *= ty
    # No AO pass: a flat material tile has no macro occluders (bakes ~0/garbage),
    # and the masonry AO is already folded into the albedo. The engine uses ao=1.
    passes = [
        ("albedo", 'DIFFUSE', False, dict(pass_filter={'COLOR'})),
        ("roughness", 'ROUGHNESS', True, {}),
        ("normal", 'NORMAL', True, {}),
    ]
    try:
        for ch, bt, non_color, kw in passes:
            img = _bake_pass(plane, res, bt, non_color, **kw)
            rel = os.path.join("materials", name, ch + ".png")
            _save_img(img, os.path.join(out_dir, rel))
            rec["maps"][ch] = rel
    finally:
        for nd, s0 in saved:
            nd.inputs["Scale"].default_value = s0
    bpy.data.objects.remove(plane, do_unlink=True)
    # Mesh UVs are world-scale (1 uv unit = 1 m); the atlas is one tile, so the
    # engine tiles it every (tx, ty) m -> uv_scale = (1/tx, 1/ty).
    rec["uv_scale"] = [1.0 / tx, 1.0 / ty]
    _write_json(os.path.join(out_dir, "materials", name + ".mat.json"), rec)
    return rec


def _write_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(obj, f, indent=1)


# --------------------------------------------------------------------------
# C scene-setup callback for the lightmap baker
# --------------------------------------------------------------------------
def emit_bake_setup_c(out_dir, manifest, mat_records, lighting, mesh_lmres):
    """Emit ``<out_dir>/scene_bake.c`` -- ONLY the generated ``lm_bake_setup_fn``
    (fixed name ``scene_bake_setup``), with NO main: the callback is passed to the
    existing baker harness (which owns EGL/GPU + main). It loads the exported
    ``.dmesh`` meshes + per-material baked albedo PNGs, fills one ``lm_mesh_t`` per
    mesh (world verts + uv0 material / uv1 lightmap), tags each with its slot-0
    material's albedo image + reflectance tint + a per-mesh lightmap resolution
    (codegen'd for the target luxel density), and sets the sun + sky + config from
    ``lighting`` (captured from the .blend). ``user`` is the export root dir."""
    mats = manifest["materials"]
    recs = {r["name"]: r for r in mat_records}
    mat_idx = {m: i for i, m in enumerate(mats)}
    objs = manifest["objects"]
    sun_dir, sun_col, sky_col = (lighting["sun_dir"], lighting["sun_color"],
                                 lighting["sky_color"])

    def cstr(s):
        return "NULL" if s is None else '"%s"' % s

    alb = [recs[m]["maps"].get("albedo") if recs[m]["maps"] else None for m in mats]
    tint = [recs[m]["tint"] for m in mats]
    mesh_file = [os.path.join("meshes", o["name"] + ".dmesh") for o in objs]
    mesh_mat = [mat_idx.get((o["materials"] or [None])[0], 0) for o in objs]

    L = []
    a = L.append
    a("/* GENERATED by scripts/export_scene.py -- do not edit.\n"
      " * The scene-setup callback (lm_bake_setup_fn) for the exported scene; pass\n"
      " * scene_bake_setup to the bake harness (scene_bake_main.c) with the export\n"
      " * dir as `user`. No main here -- the harness owns EGL/GPU + main. */\n")
    a("#include <math.h>\n#include <stdint.h>\n#include <stdio.h>\n"
      "#include <stdlib.h>\n#include <string.h>\n")
    a('#include "stb_image.h"      /* header only; impl lives in the harness TU */\n')
    a('#include "ferrum/lightmap/lm_bake_driver.h"\n'
      '#include "ferrum/lightmap/lm_mesh_bake.h"\n'
      '#include "ferrum/lightmap/lm_image.h"\n'
      '#include "ferrum/lightmap/lm_lightmap_file.h"\n'
      '#include "ferrum/math/vec3.h"\n'
      '#include "ferrum/memory/arena.h"\n'
      '#include "ferrum/mesh/dmesh_loader.h"\n')
    a("\n#define NMAT %d\n#define NMESH %d\n" % (len(mats), len(objs)))
    a("static const char *MAT_ALBEDO[NMAT] = { %s };\n"
      % ", ".join(cstr(p) for p in alb))
    a("static const float MAT_TINT[NMAT][3] = { %s };\n"
      % ", ".join("{%.5ff,%.5ff,%.5ff}" % tuple(t) for t in tint))
    a("static const char *MESH_FILE[NMESH] = { %s };\n"
      % ", ".join(cstr(f) for f in mesh_file))
    a("static const int MESH_MAT[NMESH] = { %s };\n"
      % ", ".join(str(i) for i in mesh_mat))
    a("static const unsigned MESH_LMRES[NMESH] = { %s };\n"
      % ", ".join(str(int(r)) for r in mesh_lmres))
    a("#define SUN_DIR   v3(%.5ff,%.5ff,%.5ff)\n" % tuple(sun_dir))
    a("#define SUN_COLOR v3(%.5ff,%.5ff,%.5ff)\n" % tuple(sun_col))
    a("#define SKY_COLOR v3(%.5ff,%.5ff,%.5ff)\n" % tuple(sky_col))
    a("""
static vec3_t v3(float x, float y, float z){ return (vec3_t){x,y,z}; }
static obj_mesh_t g_dm[NMESH];
static lm_mesh_t  g_lms[NMESH];
static lm_image_t g_img[NMAT];
static lm_light_t g_sun;

static void load_cpu(const char *path, lm_image_t *img){
    int w=0,h=0,n=0; unsigned char *px = stbi_load(path,&w,&h,&n,3);
    if(!px){ img->pixels=NULL; return; }
    img->pixels=px; img->width=(uint32_t)w; img->height=(uint32_t)h;
    img->channels=3; img->srgb=true;
}

/* The generated scene callback: load dmeshes + material albedo, fill the scene.
 * Sun/sky come from the .blend; lightmap resolution is codegen'd per mesh for the
 * requested luxel density (scaled at runtime by HALL_LMRES, default 1). */
bool scene_bake_setup(lm_mesh_scene_t *scene, lm_bake_config_t *cfg,
                      arena_t *arena, void *user){
    (void)arena;
    const char *root = user ? (const char*)user : ".";
    char p[1024];
    for(int m=0;m<NMAT;++m){
        if(MAT_ALBEDO[m]){ snprintf(p,sizeof p,"%s/%s",root,MAT_ALBEDO[m]);
                           load_cpu(p,&g_img[m]); }
        else g_img[m].pixels=NULL;
    }
    float lmscale = getenv("HALL_LMRES") ? (float)atof(getenv("HALL_LMRES")) : 1.0f;
    float bmin[3]={1e30f,1e30f,1e30f}, bmax[3]={-1e30f,-1e30f,-1e30f};
    int nm=0;
    for(int i=0;i<NMESH;++i){
        snprintf(p,sizeof p,"%s/%s",root,MESH_FILE[i]);
        if(dmesh_load(p,&g_dm[nm])!=0) continue;
        int mi = MESH_MAT[i];
        memset(&g_lms[nm],0,sizeof(lm_mesh_t));
        g_lms[nm].positions=g_dm[nm].positions; g_lms[nm].normals=g_dm[nm].normals;
        g_lms[nm].uv0=g_dm[nm].uvs; g_lms[nm].uv1=g_dm[nm].uvs1;
        g_lms[nm].indices=g_dm[nm].indices; g_lms[nm].vert_count=g_dm[nm].vert_count;
        g_lms[nm].index_count=g_dm[nm].index_count;
        g_lms[nm].albedo_image = g_img[mi].pixels ? &g_img[mi] : NULL;
        g_lms[nm].albedo = v3(MAT_TINT[mi][0],MAT_TINT[mi][1],MAT_TINT[mi][2]);
        g_lms[nm].emissive = v3(0,0,0); g_lms[nm].material=0;
        g_lms[nm].lightmap_resolution=(uint32_t)(lmscale*(float)MESH_LMRES[i]+0.5f);
        for(uint32_t v=0;v<g_dm[nm].vert_count;++v) for(int c=0;c<3;++c){
            float q=g_dm[nm].positions[v*3+c];
            if(q<bmin[c]) bmin[c]=q; if(q>bmax[c]) bmax[c]=q; }
        ++nm;
    }
    if(nm==0) return false;
    float diag=sqrtf((bmax[0]-bmin[0])*(bmax[0]-bmin[0])+
                     (bmax[1]-bmin[1])*(bmax[1]-bmin[1])+
                     (bmax[2]-bmin[2])*(bmax[2]-bmin[2]));
    memset(&g_sun,0,sizeof g_sun); g_sun.kind=LM_LIGHT_DIRECTIONAL;
    g_sun.direction=SUN_DIR; g_sun.color=SUN_COLOR;
    lm_material_t fb={{0,0,0},{0,0,0}};
    *scene=(lm_mesh_scene_t){ g_lms,(uint32_t)nm,&g_sun,1,{NULL,0,fb} };

    memset(cfg,0,sizeof *cfg);
    float pad=1.0f;
    cfg->svo_bounds=(phys_aabb_t){ {bmin[0]-pad,bmin[1]-pad,bmin[2]-pad},
                                   {bmax[0]+pad,bmax[1]+pad,bmax[2]+pad} };
    cfg->voxel_size=getenv("HALL_VOXEL")?(float)atof(getenv("HALL_VOXEL")):0.06f;
    cfg->atlas_width=4096; cfg->atlas_padding=2; cfg->direct_samples=0;
    cfg->farfield_samples=getenv("HALL_SAMPLES")?(uint32_t)atoi(getenv("HALL_SAMPLES")):2048u;
    cfg->gi_bounces=getenv("HALL_BOUNCES")?(uint32_t)atoi(getenv("HALL_BOUNCES")):8u;
    cfg->gi_threads=getenv("HALL_THREADS")?(uint32_t)atoi(getenv("HALL_THREADS")):0u;
    cfg->farfield_near=0.5f*diag; cfg->farfield_maxdist=1e9f; cfg->seed=11u;
    cfg->sky.kind=LM_SKY_CONSTANT; cfg->sky.color=SKY_COLOR;
    cfg->gi_batch=getenv("HALL_BATCH")?(uint32_t)atoi(getenv("HALL_BATCH")):64u;
    /* Persist the near-field SDF chunks (<HALL_SDF>_cNNN.sdf) so the runtime's
     * dynamic SDF-probe GI can trace them; HALL_CHUNK sets the chunk edge (m). */
    cfg->chunk_size=getenv("HALL_CHUNK")?(float)atof(getenv("HALL_CHUNK")):0.0f;
    cfg->chunk_margin=getenv("HALL_CHUNK_MARGIN")?(float)atof(getenv("HALL_CHUNK_MARGIN")):2.0f;
    cfg->sdf_out_prefix=getenv("HALL_SDF");
    printf("bake: %d meshes voxel=%.3f samples=%u bounces=%u chunk=%.1f diag=%.2f\\n",
           nm,cfg->voxel_size,cfg->farfield_samples,cfg->gi_bounces,cfg->chunk_size,diag);
    fflush(stdout);
    return true;
}
""")
    path = os.path.join(out_dir, "scene_bake.c")
    with open(path, "w") as f:
        f.write("".join(L))
    return path


# --------------------------------------------------------------------------
# Orchestrator
# --------------------------------------------------------------------------
def export_scene(collection_name, out_dir, tiles=None, bake_res=1024,
                 scene_callback=None, bake_materials=True):
    """Export every mesh of ``collection_name`` + its materials + the manifest.

    ``scene_callback`` -- optional zero-arg callable that (re)generates the scene
    before export (e.g. ``great_hall.build_hall_scene``). It lets the exporter run
    headless from an empty .blend: regenerate the geometry via the EXISTING
    generator, then record each mesh's placement straight from Blender. When given,
    the collection it builds must be named ``collection_name``.

    ``bake_materials`` False skips the slow Cycles material bake and reuses the
    map PNGs already on disk -- for iterating on meshes/lightmap-UVs/descriptor
    without re-baking materials."""
    tiles = DEFAULT_TILES if tiles is None else tiles
    if scene_callback is not None:
        scene_callback()
    col = bpy.data.collections.get(collection_name)
    if col is None:
        raise RuntimeError(f"collection {collection_name!r} not found")
    os.makedirs(out_dir, exist_ok=True)

    # A camera must exist for Cycles to render/bake (a scene with no camera bakes
    # PURE BLACK). Create a throwaway one if the scene has none.
    if bpy.context.scene.camera is None:
        cam_data = bpy.data.cameras.new("_bake_cam")
        cam_obj = bpy.data.objects.new("_bake_cam", cam_data)
        bpy.context.scene.collection.objects.link(cam_obj)
        bpy.context.scene.camera = cam_obj

    # 1. materials (unique, deterministic order)
    mats = []
    seen = set()
    for o in col.objects:
        if o.type != 'MESH':
            continue
        for slot in o.data.materials:
            if slot and slot.name not in seen:
                seen.add(slot.name)
                mats.append(slot)
    mat_records = [export_material(m, out_dir, tiles, bake_res, bake=bake_materials)
                   for m in mats]

    # Lightmap UVs FIRST: generate the non-overlapping 'lightmap' unwrap on every
    # mesh BEFORE the fvma export so the client's .fvma carries uv1 (the lightmap
    # coordinates). The unwrap is idempotent (scene_demo reuses it in step 3), so
    # the .fvma, the .dmesh (baker input) and the baked atlas rects share ONE
    # lightmap UV. Without this the .fvma has no lightmap coords and the baked GI
    # cannot be sampled -- the "lightmap_unpack" the client needs (rpg-jro2).
    import scene_demo
    for o in (o for o in col.objects if o.type == 'MESH'):
        scene_demo._gen_lightmap_uv(o)

    # 2. meshes + manifest
    objs = []
    mesh_lmres = []
    # STATIC objects first (alphabetical), DYNAMIC ones last. Dynamic props take no
    # lightmap slot, so keeping them after every static mesh means adding/removing
    # one never shifts a static mesh's index -- and the baked lightmap's per-mesh
    # atlas rects (indexed by that order) stay valid, i.e. no re-bake.
    for o in sorted((o for o in col.objects if o.type == 'MESH'),
                    key=lambda o: (1 if bool(o.get("ferrum_dynamic", 0)) else 0, o.name)):
        rel, vc, fc = export_mesh(o, out_dir)
        loc, rot_q, scale = o.matrix_world.decompose()
        lmr = _lmres_for(o)
        mesh_lmres.append(lmr)
        rec = {
            "name": o.name,
            "mesh": rel,
            "position": _engine_pos(loc),
            "rotation": _engine_quat(rot_q),
            "scale": [float(scale.x), float(scale.z), float(scale.y)],
            "materials": [m.name if m else None for m in o.data.materials],
            "lightmap_res": lmr,
            "vertices": vc, "faces": fc,
        }
        # DYNAMIC objects (ferrum_dynamic custom property): excluded from the offline
        # bake -- the runtime voxelises them into its dynamic albedo volume instead,
        # so their colour still bleeds through the probe GI.
        if bool(o.get("ferrum_dynamic", 0)):
            rec["dynamic"] = True
        objs.append(rec)

    manifest = {"collection": collection_name,
                "materials": [m.name for m in mats],
                "objects": objs}
    _write_json(os.path.join(out_dir, "scene.json"), manifest)

    # Full engine level descriptor (<collection>.scene) -- the exporter-produced
    # single source of truth loaded identically by server + client. Emits every
    # section: full material PBR defs, objects (bake order), ALL lights (incl. the
    # sun, baked into the lightmap), dynamic-object colliders, baked light-data
    # refs, and the probe-placement spec. Nothing is hand-authored downstream.
    scn = bpy.context.scene
    descriptor = {
        "name": collection_name,
        "materials": [_material_desc(r) for r in mat_records],
        "objects": objs,
        "lights": _export_lights(),
        "colliders": _export_colliders(col),
        # Baker output naming: <collection>.flm lightmap + <collection>.flm_cNNN.sdf.
        "lightmap": {"prefix": collection_name + ".flm", "perchunk": False,
                     "manifest": ""},
        "sdf": {"prefix": collection_name + ".flm"},
        # Probe spacing is level-tuning: read from scene props if authored, else
        # engine defaults. Manual probes / importance boxes are authored separately.
        "probes": {"spacing": float(scn.get("ferrum_probe_spacing", 1.0)),
                   "vspacing": float(scn.get("ferrum_probe_vspacing", 0.75))},
    }
    _write_json(os.path.join(out_dir, collection_name + ".scene"), descriptor)

    # 3. dual-UV .dmesh export (world-space + generated lightmap UVs) via the
    #    existing exporter -- what the lightmap baker consumes.
    import scene_demo
    scene_demo.export_scene(col, os.path.join(out_dir, "meshes"))

    # 4. emit the C scene-setup callback (passed to the bake harness).
    emit_bake_setup_c(out_dir, manifest, mat_records, _read_lighting(),
                      mesh_lmres)

    print(f"[export_scene] {len(objs)} meshes, {len(mats)} materials, "
          f"+dmesh +scene_bake.c -> {out_dir}", flush=True)
    return manifest


if __name__ == "__main__" or "OUT" in globals():
    _out = globals().get("OUT", "/home/kmd/rpg/datasets/great_hall_export")
    _col = globals().get("COLLECTION", "great_hall")
    _res = int(globals().get("BAKE_RES", 1024))
    # BAKE_MATERIALS=0 (env or global) reuses on-disk maps -- fast geometry iteration.
    _bake = str(globals().get("BAKE_MATERIALS",
                              os.environ.get("BAKE_MATERIALS", "1"))) not in ("0", "false", "")
    export_scene(_col, _out, bake_res=_res, bake_materials=_bake)
