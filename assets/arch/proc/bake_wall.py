"""Bake the high-poly hewn-brick wall down to tileable material maps.

Consumes a wall built by ``brick_wall.build_wall`` (bricks + a recessed mortar
plane) and produces, over the seamless tile ``[0, W] x [0, H]``:

  * ``mask``   -- white = brick, black = mortar (the layer mask).
  * ``height`` -- front-facing depth of the relief (white = proud).
  * ``normal`` -- tangent-space normal map of the relief.
  * ``ao``     -- ambient occlusion.

Between the mask and the map bakes it runs a mortar "clumpiness" pass: bed joints
(thin horizontal mortar squeezed between the courses) are detected from the mask,
propagated a little into the surrounding mortar, modulated by multifractal noise
scaled by that compression, and used to displace the mortar plane outward -- so
the mortar bulges where it was squeezed rather than sitting dead flat.

Everything is an ORTHOGRAPHIC front projection of the tile, which is exactly what
a tileable height/normal map represents for a near-planar wall. Runs in Blender
(numpy only -- no scipy -- so it works in the bundled interpreter)::

    ns = {}
    exec(open("assets/arch/proc/bake_wall.py").read(), ns)
    ns["bake_wall"](width=2.0, height=1.15, res=2048,
                    out_dir="assetsrc/materials/stone_wall/bake")
"""
import os

import bpy
import numpy as np


# --------------------------------------------------------------------------
# Camera + render setup
# --------------------------------------------------------------------------
def _ortho_cam(W, H, name="bake_cam", back=-4.0):
    """A front orthographic camera framing exactly [0,W] x [0,H] (looking +Y)."""
    cam = bpy.data.objects.get(name)
    if cam is None:
        cam = bpy.data.objects.new(name, bpy.data.cameras.new(name))
        bpy.context.scene.collection.objects.link(cam)
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = max(W, H)          # long side matches long resolution
    cam.location = (W * 0.5, back, H * 0.5)
    cam.rotation_euler = (np.pi / 2.0, 0.0, 0.0)   # -Z -> +Y, up -> +Z
    return cam


def _res(W, H, res):
    """Resolution (long side = ``res``) matching the tile aspect."""
    if W >= H:
        return int(res), max(1, int(round(res * H / W)))
    return max(1, int(round(res * W / H))), int(res)


# --------------------------------------------------------------------------
# Emission materials (bake data straight out of the shader, no lights/compositor)
# --------------------------------------------------------------------------
def _emit_mat(name):
    """A fresh material whose surface is a single Emission; returns (mat, nodes,
    links, emission_node) so callers can drive the emission colour."""
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    emit = nt.nodes.new('ShaderNodeEmission')
    nt.links.new(emit.outputs['Emission'], out.inputs['Surface'])
    return mat, nt.nodes, nt.links, emit


def _flat_mat(name, value):
    mat, nodes, links, emit = _emit_mat(name)
    emit.inputs['Color'].default_value = (value, value, value, 1.0)
    return mat


def _height_mat(name, y_front, y_back):
    """Emission = remapped front depth: y_front (proud, toward camera) -> 1,
    y_back (recessed) -> 0."""
    mat, nodes, links, emit = _emit_mat(name)
    geo = nodes.new('ShaderNodeNewGeometry')
    sep = nodes.new('ShaderNodeSeparateXYZ')
    links.new(geo.outputs['Position'], sep.inputs['Vector'])
    mr = nodes.new('ShaderNodeMapRange')
    mr.clamp = True
    mr.inputs['From Min'].default_value = y_front
    mr.inputs['From Max'].default_value = y_back
    mr.inputs['To Min'].default_value = 1.0
    mr.inputs['To Max'].default_value = 0.0
    links.new(sep.outputs['Y'], mr.inputs['Value'])
    links.new(mr.outputs['Result'], emit.inputs['Color'])
    return mat


def _normal_mat(name):
    """Emission = tangent-space normal. The wall faces -Y, so map world (nx,ny,nz)
    -> tangent (nx, nz, -ny) (wall normal -> +Z/blue), then *0.5+0.5."""
    mat, nodes, links, emit = _emit_mat(name)
    geo = nodes.new('ShaderNodeNewGeometry')
    sep = nodes.new('ShaderNodeSeparateXYZ')
    links.new(geo.outputs['Normal'], sep.inputs['Vector'])
    negy = nodes.new('ShaderNodeMath')
    negy.operation = 'MULTIPLY'
    negy.inputs[1].default_value = -1.0
    links.new(sep.outputs['Y'], negy.inputs[0])
    comb = nodes.new('ShaderNodeCombineXYZ')
    links.new(sep.outputs['X'], comb.inputs['X'])
    links.new(sep.outputs['Z'], comb.inputs['Y'])
    links.new(negy.outputs['Value'], comb.inputs['Z'])
    mad = nodes.new('ShaderNodeVectorMath')
    mad.operation = 'MULTIPLY_ADD'
    mad.inputs[1].default_value = (0.5, 0.5, 0.5)
    mad.inputs[2].default_value = (0.5, 0.5, 0.5)
    links.new(comb.outputs['Vector'], mad.inputs[0])
    links.new(mad.outputs['Vector'], emit.inputs['Color'])
    return mat


def _ao_mat(name, distance=0.05, samples=8):
    """Emission = Cycles ambient occlusion (grayscale)."""
    mat, nodes, links, emit = _emit_mat(name)
    ao = nodes.new('ShaderNodeAmbientOcclusion')
    ao.samples = samples
    ao.inputs['Distance'].default_value = distance
    links.new(ao.outputs['AO'], emit.inputs['Color'])
    return mat


def _assign(objs, mat):
    """Override every object's material at OBJECT level (shared meshes keep their
    own data material; this lets one bake material paint all instances)."""
    for o in objs:
        if not o.material_slots:
            o.data.materials.append(None)
        o.material_slots[0].link = 'OBJECT'
        o.material_slots[0].material = mat


def _tint_bricks(bricks, shades=(1.0, 0.86, 0.72, 0.6)):
    """Tint bricks with several distinct bright shades so touching stones always
    differ in value and segment apart even when the mortar joint is hair-thin.
    Two shades can't do it (an offset bond isn't 2-colourable), so cycle N shades
    by ``pos + 2*course``: neighbours in a row step by 1, neighbours across a row
    step by 2, and the half-brick-offset diagonals rarely collide with 4 shades.
    Object names are ``..._{course}_{n}``. All shades are >0.5 so the mask still
    thresholds cleanly to brick-vs-mortar."""
    import re
    mats = [_flat_mat(f"bake_tint{k}", v) for k, v in enumerate(shades)]
    courses = {}
    for o in bricks:
        m = re.search(r'_(\d+)_\d+', o.name)
        courses.setdefault(int(m.group(1)) if m else 0, []).append(o)
    for c, objs in courses.items():
        objs.sort(key=lambda o: o.matrix_world.translation.x)
        for i, o in enumerate(objs):
            _assign([o], mats[(i + 2 * c) % len(mats)])


# --------------------------------------------------------------------------
# Render a frame to a PNG
# --------------------------------------------------------------------------
def _render(cam, rx, ry, path, engine='BLENDER_EEVEE', samples=16,
            raw=True):
    sc = bpy.context.scene
    sc.camera = cam
    sc.render.engine = engine
    sc.render.resolution_x = rx
    sc.render.resolution_y = ry
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = False
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGB'
    if engine == 'CYCLES':
        sc.cycles.samples = samples
    else:
        try:
            sc.eevee.taa_render_samples = samples
        except AttributeError:
            pass
    prev_vt = sc.view_settings.view_transform
    sc.view_settings.view_transform = 'Raw' if raw else 'Standard'
    world = sc.world
    prev_world = world.color if world else None
    if world:
        world.color = (0.0, 0.0, 0.0)
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    sc.view_settings.view_transform = prev_vt
    if world and prev_world is not None:
        world.color = prev_world
    return path


def _enable_gpu():
    """Point Cycles at a GPU (OptiX/CUDA) if one is available; else stay on CPU.
    Returns the backend used ('OPTIX'/'CUDA'/None)."""
    try:
        prefs = bpy.context.preferences.addons['cycles'].preferences
    except (KeyError, AttributeError):
        return None
    for backend in ('OPTIX', 'CUDA'):
        try:
            prefs.compute_device_type = backend
            prefs.get_devices()
            gpus = [d for d in prefs.devices if d.type == backend]
            if gpus:
                for d in prefs.devices:
                    d.use = (d.type == backend)
                bpy.context.scene.cycles.device = 'GPU'
                return backend
        except (TypeError, RuntimeError):
            continue
    return None


# --------------------------------------------------------------------------
# Mortar clumpiness (numpy, no scipy)
# --------------------------------------------------------------------------
def _load_gray(path):
    """Load a PNG into a top-down (H, W) float array from its red channel."""
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
    bpy.data.images.remove(img)
    return px[::-1, :, 0]                       # flip bottom-up -> top-down


def _blur3(a):
    p = np.pad(a, 1, mode='edge')
    return (p[:-2, :-2] + p[:-2, 1:-1] + p[:-2, 2:] +
            p[1:-1, :-2] + p[1:-1, 1:-1] + p[1:-1, 2:] +
            p[2:, :-2] + p[2:, 1:-1] + p[2:, 2:]) / 9.0


def _value_noise(h, w, cells, rng):
    g = rng.random((cells + 1, cells + 1))
    ys = np.linspace(0, cells, h)
    xs = np.linspace(0, cells, w)
    y0 = np.floor(ys).astype(int)
    x0 = np.floor(xs).astype(int)
    y1 = np.minimum(y0 + 1, cells)
    x1 = np.minimum(x0 + 1, cells)
    fy = (ys - y0)[:, None]
    fx = (xs - x0)[None, :]
    g00 = g[y0][:, x0]
    g01 = g[y0][:, x1]
    g10 = g[y1][:, x0]
    g11 = g[y1][:, x1]
    top = g00 * (1 - fx) + g01 * fx
    bot = g10 * (1 - fx) + g11 * fx
    return top * (1 - fy) + bot * fy


def _fractal_noise(h, w, seed, base_cells=6, octaves=4):
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w))
    amp, tot, cells = 1.0, 0.0, base_cells
    for _ in range(octaves):
        out += amp * _value_noise(h, w, cells, rng)
        tot += amp
        amp *= 0.5
        cells *= 2
    return out / tot


def compute_clump(mask, run_thresh_px, propagate=3, seed=0, base_cells=6):
    """Mortar clumpiness field. Bed joints (thin horizontal mortar squeezed
    between courses) have a SHORT vertical run -> high compression; head joints
    (tall vertical mortar) have a long run -> low. The compression is propagated
    a little into the surrounding mortar then modulated by multifractal noise
    scaled by the compression, so clumps grow where the mortar was squeezed."""
    brick = mask > 0.5
    h, w = mask.shape
    up = np.zeros((h, w))
    cnt = np.zeros(w)
    for r in range(h):
        cnt = np.where(brick[r], 0.0, cnt + 1.0)
        up[r] = cnt
    down = np.zeros((h, w))
    cnt = np.zeros(w)
    for r in range(h - 1, -1, -1):
        cnt = np.where(brick[r], 0.0, cnt + 1.0)
        down[r] = cnt
    run = up + down - 1.0
    comp = np.clip(1.0 - run / max(1.0, run_thresh_px), 0.0, 1.0)
    comp[brick] = 0.0
    for _ in range(propagate):
        comp = _blur3(comp)
    noise = _fractal_noise(h, w, seed, base_cells)
    clump = np.clip(comp, 0.0, 1.0) * noise
    clump[brick] = 0.0
    return clump


def displace_mortar(mortar, clump, W, H, amp):
    """Push each mortar vertex outward (toward -Y) by the clump value sampled at
    its (x, z). ``clump`` is a top-down (h, w) array over the tile."""
    hpx, wpx = clump.shape
    for v in mortar.data.vertices:
        u = min(max(v.co.x / W, 0.0), 1.0)
        t = min(max(v.co.z / H, 0.0), 1.0)
        col = min(wpx - 1, int(u * (wpx - 1)))
        row = min(hpx - 1, int((1.0 - t) * (hpx - 1)))
        v.co.y -= float(clump[row, col]) * amp
    mortar.data.update()


# --------------------------------------------------------------------------
# Orchestrator
# --------------------------------------------------------------------------
def bake_wall(width=2.0, height=1.15, seed=4, res=2048, out_dir=None,
              clump=0.7, mortar_depth=0.02, prefab_dir=None, name="brick_wall"):
    """Build a wall then bake mask / height / normal / ao maps over its tile.
    Returns a dict of the written file paths."""
    here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() \
        else "/home/kmd/rpg/assets/arch/proc"
    if out_dir is None:
        out_dir = os.path.join(here, "prefabs", "bake")
    os.makedirs(out_dir, exist_ok=True)

    ns = {}
    with open(os.path.join(here, "brick_wall.py")) as f:
        exec(compile(f.read(), "brick_wall.py", "exec"), ns)
    kw = {"width": width, "height": height, "seed": seed,
          "mortar_depth": mortar_depth, "name": name}
    if prefab_dir:
        kw["prefab_dir"] = prefab_dir
    wall = ns["build_wall"](**kw)
    col, mortar = wall["collection"], wall["mortar"]
    W, Hh = wall["tile_width"], wall["height"]
    bricks = [o for o in col.objects if o is not mortar]

    # Hide everything else so only the wall renders.
    wall_objs = set(col.objects)
    for o in bpy.data.objects:
        o.hide_render = o not in wall_objs
    _enable_gpu()
    cam = _ortho_cam(W, Hh)
    rx, ry = _res(W, Hh, res)
    out = {}

    # 1. brick-vs-mortar mask. Bricks get several bright shades so every touching
    # pair has a value step at the boundary (segmentable); mortar is black.
    _tint_bricks(bricks)
    _assign([mortar], _flat_mat("bake_black", 0.0))
    out["mask"] = _render(cam, rx, ry, os.path.join(out_dir, "mask.png"),
                          engine='CYCLES', samples=16)

    # 2. clumpiness -> displace mortar (bed-joint width in px sets the threshold)
    mask = _load_gray(out["mask"])
    bed_px = max(4.0, (mortar_depth * 3.0) / Hh * ry)
    field = compute_clump(mask, run_thresh_px=bed_px * 1.6, propagate=3,
                          seed=seed, base_cells=6)
    displace_mortar(mortar, field, W, Hh, amp=mortar_depth * 0.8 * clump)

    # front / back depth range of the relief (bricks face -Y). Front = most
    # negative world Y over the brick bounding boxes; back = the mortar plane.
    from mathutils import Vector
    y_front = min((o.matrix_world @ Vector(corner)).y
                  for o in bricks for corner in o.bound_box)
    y_back = float(mortar.data.vertices[0].co.y)

    # 3. height / normal / ao
    _assign(col.objects, _height_mat("bake_height", y_front, y_back))
    out["height"] = _render(cam, rx, ry, os.path.join(out_dir, "height.png"),
                            engine='CYCLES', samples=16)
    _assign(col.objects, _normal_mat("bake_normal"))
    out["normal"] = _render(cam, rx, ry, os.path.join(out_dir, "normal.png"),
                            engine='CYCLES', samples=16)
    _assign(col.objects, _ao_mat("bake_ao", distance=0.06, samples=8))
    out["ao"] = _render(cam, rx, ry, os.path.join(out_dir, "ao.png"),
                        engine='CYCLES', samples=64, raw=False)
    return out
