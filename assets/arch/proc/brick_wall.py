"""Lay baked hewn-brick prefabs into a Romanesque running-bond wall.

Bricks are NOT generated here -- that is far too slow. This module consumes the
prefab library baked by ``gen_brick_prefabs.py`` (a manifest plus one OBJ per
brick) and arranges instances of them into a wall, choosing which brick to place
next so that adjacent ends fit together.

Fitting by side colour
----------------------
Each prefab records the mean outward normal of its two chiselled ends (-X, +X).
When a brick B is placed to the right of brick A, we want A's +X end and B's -X
end to be roughly parallel facing planes so the head joint is an even mortar gap.
Two facing ends are parallel when B's -X normal is the negation of A's +X normal,
so we bucket ends by the quantised tilt of their normal (the "side colour") and
look for a B whose left colour is the complement of A's right colour. A brick may
also be placed flipped 180 degrees about Z, which swaps and reflects its ends and
so doubles the pool of usable orientations. A nearest-normal fallback guarantees
a placement is always found.

Meshes are shared: each prefab OBJ is imported at most once and every placement
of it is a lightweight object referencing that one mesh datablock.

Typical use inside a running Blender session::

    ns = {}
    exec(open("assets/arch/proc/brick_wall.py").read(), ns)
    ns["build_wall"](width=2.0, courses=10, seed=1)
"""
import json
import math
import os

import bpy
import numpy as np

try:
    HERE = os.path.dirname(os.path.abspath(__file__))
except NameError:
    # Exec'd as a string (e.g. via the Blender MCP) where __file__ is unset.
    HERE = os.path.join(os.path.dirname(bpy.data.filepath) or os.getcwd(),
                        "assets", "arch", "proc")
    if not os.path.isdir(os.path.join(HERE, "prefabs")):
        HERE = "/home/kmd/rpg/assets/arch/proc"
PREFAB_DIR = os.path.join(HERE, "prefabs", "bricks")


# --------------------------------------------------------------------------
# Prefab library + orientations
# --------------------------------------------------------------------------
def load_manifest(prefab_dir=PREFAB_DIR):
    """Read the prefab manifest. Returns the parsed dict; raises if absent."""
    path = os.path.join(prefab_dir, "manifest.json")
    with open(path) as f:
        return json.load(f)


def _rot_bbox(lo, hi, flip):
    """Local bbox after the placement rotation (180 about Z if ``flip``)."""
    if not flip:
        return list(lo), list(hi)
    # (x, y, z) -> (-x, -y, z)
    return [-hi[0], -hi[1], lo[2]], [-lo[0], -lo[1], hi[2]]


def _reflect_z(n):
    """A normal after a 180-degree rotation about Z: (nx,ny,nz)->(-nx,-ny,nz)."""
    return [-n[0], -n[1], n[2]]


def build_orientations(manifest):
    """Expand every brick into its placeable orientations (normal + Z-flip).

    Each orientation carries the outward normals and quantised colours of its
    left (-X) and right (+X) ends *as placed*, plus its rotated local bbox."""
    tbin = manifest["tilt_bin"]

    def colour(n):
        return (int(round(n[1] / tbin)), int(round(n[2] / tbin)))

    orientations = []
    for b in manifest["bricks"]:
        lo, hi = b["bbox"]
        endL_n = b["endL"]["n"]
        endR_n = b["endR"]["n"]
        # Unflipped: left end is the original -X end, right is the original +X.
        orientations.append({
            "brick": b, "flip": False,
            "left_n": endL_n, "right_n": endR_n,
            "left_c": colour(endL_n), "right_c": colour(endR_n),
            "lo": _rot_bbox(lo, hi, False)[0], "hi": _rot_bbox(lo, hi, False)[1],
        })
        # Flipped 180 about Z: the original +X end now faces -X (and is
        # reflected), becoming the left end; the original -X end becomes right.
        fL = _reflect_z(endR_n)
        fR = _reflect_z(endL_n)
        orientations.append({
            "brick": b, "flip": True,
            "left_n": fL, "right_n": fR,
            "left_c": colour(fL), "right_c": colour(fR),
            "lo": _rot_bbox(lo, hi, True)[0], "hi": _rot_bbox(lo, hi, True)[1],
        })
    return orientations


def _index_by_left_colour(orientations, aspect=None):
    """Map left-end colour -> list of orientation indices (optionally filtered
    to a single aspect id)."""
    index = {}
    for i, o in enumerate(orientations):
        if aspect is not None and o["brick"]["aspect"] != aspect:
            continue
        index.setdefault(o["left_c"], []).append(i)
    return index


# --------------------------------------------------------------------------
# Mesh sharing
# --------------------------------------------------------------------------
def _import_mesh(brick, cache, prefab_dir):
    """Import a prefab OBJ once and return its (shared) mesh datablock."""
    bid = brick["id"]
    if bid in cache:
        return cache[bid]
    before = set(bpy.data.objects)
    bpy.ops.wm.obj_import(filepath=os.path.join(prefab_dir, brick["obj"]),
                          forward_axis='Y', up_axis='Z')
    imported = [o for o in bpy.data.objects if o not in before]
    obj = imported[0]
    mesh = obj.data
    mesh.name = brick["name"]
    # Keep only the mesh; drop the import wrapper objects.
    for o in imported:
        bpy.data.objects.remove(o, do_unlink=True)
    cache[bid] = mesh
    return mesh


# --------------------------------------------------------------------------
# Selection
# --------------------------------------------------------------------------
def _pick_next(orientations, index, global_index, prev, rng):
    """Choose the next orientation to the right of ``prev`` (or any, if prev is
    None) from ``index`` (typically one aspect's pool). Prefers exact
    complementary side-colour matches within ``index``, then within
    ``global_index`` (any aspect), then nearest by facing-normal dot product."""
    if prev is None:
        pool = [i for lst in index.values() for i in lst] or \
            list(range(len(orientations)))
        weights = np.array([orientations[i]["brick"]["dims"][0] for i in pool])
        return int(rng.choice(pool, p=weights / weights.sum()))

    ar = prev["right_n"]
    want = (int(round(-ar[1] / _TILT_BIN)), int(round(-ar[2] / _TILT_BIN)))
    bucket = index.get(want) or (global_index.get(want) if global_index else None)
    if bucket:
        return int(rng.choice(bucket))

    # Fallback: maximise dot(B.left_n, -A.right_n) -> parallel facing planes.
    target = np.array([-ar[0], -ar[1], -ar[2]])
    best_i, best_s = 0, -2.0
    for i, o in enumerate(orientations):
        s = float(np.dot(o["left_n"], target))
        if s > best_s:
            best_i, best_s = i, s
    return best_i


def _stagger_aspect(cursor, tol, prev_joints, lengths, main_aspect, variety,
                    rng, min_stagger):
    """Pick which aspect (stretcher length) to place next so that this brick's
    HEAD JOINT does not land on a head joint of the course below -- the running-
    bond stagger. Prefers the main stretcher when it already clears; otherwise
    chooses among the lengths whose resulting joint clears by ``min_stagger``,
    falling back to the length with the most clearance."""
    if not prev_joints:
        return main_aspect if rng.random() > variety \
            else int(rng.integers(0, len(lengths)))

    def clearance(a):
        j = cursor + tol + lengths[a]
        return min(abs(j - pj) for pj in prev_joints)

    if clearance(main_aspect) >= min_stagger and rng.random() > variety:
        return main_aspect
    order = sorted(range(len(lengths)), key=clearance, reverse=True)
    clear = [a for a in order if clearance(a) >= min_stagger]
    return int(rng.choice(clear)) if clear else order[0]


# --------------------------------------------------------------------------
# Wall assembly
# --------------------------------------------------------------------------
_TILT_BIN = 0.07  # overwritten from the manifest in build_wall


def _best_fit(orientations, index, prev, target_left, limit):
    """Pick the longest orientation whose right end still lands at/under
    ``limit`` when its left end is at ``target_left`` -- used to close out a
    course tightly. Prefers a colour match to ``prev`` but falls back to any.
    Returns an orientation index, or None if even the shortest overshoots."""
    remaining = limit - target_left
    candidates = None
    if prev is not None:
        ar = prev["right_n"]
        want = (int(round(-ar[1] / _TILT_BIN)), int(round(-ar[2] / _TILT_BIN)))
        candidates = index.get(want)
    pool = candidates if candidates else range(len(orientations))
    best_i, best_len = None, -1.0
    for i in pool:
        o = orientations[i]
        length = o["hi"][0] - o["lo"][0]
        if length <= remaining and length > best_len:
            best_i, best_len = i, length
    if best_i is None and candidates:
        # No matching brick fits; widen the search to every orientation.
        return _best_fit(orientations, index, None, target_left, limit)
    return best_i


def _support_offsets(mesh, n_left, n_right, cache, bid):
    """Signed support-plane offsets for a brick's two ends. For each end we fit
    the plane with that end's own normal that minimally bounds the end (the
    supporting plane, i.e. the max projection of the end vertices onto the
    normal). For a tilted-but-flat hewn face this plane sits *on the face*, so it
    is not inflated by whichever corner happens to poke furthest along X. The two
    scalars (dL along the -X normal, dR along the +X normal) are rotation
    invariant, so a 180-degree Z flip merely swaps them. Cached per brick id."""
    if bid in cache:
        return cache[bid]
    co = np.array([v.co for v in mesh.vertices], dtype=np.float64)
    x = co[:, 0]
    xmin, xmax = float(x.min()), float(x.max())
    band = 0.35 * (xmax - xmin)
    left = co[x < xmin + band]
    right = co[x > xmax - band]
    d_left = float((left @ np.array(n_left)).max())
    d_right = float((right @ np.array(n_right)).max())
    cache[bid] = (d_left, d_right)
    return d_left, d_right


def _placed_supports(o, mesh, cache):
    """Placed-frame (left, right) support offsets -- how far the left/right end
    faces extend from the brick origin. A Z flip swaps the two ends."""
    b = o["brick"]
    d_left, d_right = _support_offsets(mesh, b["endL"]["n"], b["endR"]["n"],
                                       cache, b["id"])
    return (d_right, d_left) if o["flip"] else (d_left, d_right)


def _place(collection, name, mesh, flip, rot, loc):
    """Create one brick instance sharing ``mesh`` at the given transform."""
    inst = bpy.data.objects.new(name, mesh)
    collection.objects.link(inst)
    rz = math.pi if flip else 0.0
    inst.rotation_euler = (rot[0], rot[1], rz + rot[2])
    inst.location = loc
    return inst


def _rand_tilt(rng, tilt_deg, tilt_frac):
    """A small random (rx, ry, rz) tilt in radians for a fraction of bricks.
    Yaw (rz) is kept to half range so head joints do not gape open."""
    if tilt_deg <= 0.0 or rng.random() > tilt_frac:
        return (0.0, 0.0, 0.0)
    t = math.radians(tilt_deg)
    return (float(rng.uniform(-t, t)), float(rng.uniform(-t, t)),
            float(rng.uniform(-0.5 * t, 0.5 * t)))


def _build_mortar(name, x0, width, height, front_y, mortar_depth, cell,
                  collection):
    """A flat, finely subdivided mortar plane spanning the wall in XZ, set back
    ``mortar_depth`` behind the brick front faces. Subdivision (``cell`` metres)
    leaves it ready for the clumpiness displacement pass. Returns the object."""
    import bmesh
    nx = max(2, int(round(width / cell)))
    nz = max(2, int(round(height / cell)))
    y = front_y + mortar_depth
    bm = bmesh.new()
    verts = [[None] * (nx + 1) for _ in range(nz + 1)]
    for iz in range(nz + 1):
        for ix in range(nx + 1):
            vx = x0 + width * ix / nx
            vz = height * iz / nz
            verts[iz][ix] = bm.verts.new((vx, y, vz))
    bm.verts.ensure_lookup_table()
    for iz in range(nz):
        for ix in range(nx):
            bm.faces.new((verts[iz][ix], verts[iz][ix + 1],
                          verts[iz + 1][ix + 1], verts[iz + 1][ix]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    return obj


def build_wall(width=4.0, height=3.0, seed=0, mortar=0.0015, bed=0.003,
               mortar_depth=0.02, seat_jitter=0.002, depth_jitter=0.004,
               tilt_deg=2.0, tilt_frac=0.5, seamless=True, mortar_cell=0.006,
               main_aspect=1, variety=0.25, min_stagger_frac=0.22,
               name="brick_wall", prefab_dir=PREFAB_DIR, collection=None):
    """Assemble a running-bond wall ``width`` x ``height`` metres, plus a
    recessed mortar plane behind it.

    ``width`` and ``height`` (metres) drive the wall size: the width sets how
    many bricks fill each course, and the height sets the number of courses
    (from the prefab course height + bed joint). Features:
      * Seamless horizontal tiling: when ``seamless`` each course's final brick
        is an exact duplicate of its first brick placed one ``width`` later, so
        cropping to [0, width] repeats without a visible join. Alternate courses
        straddle the seam (running-bond offset); because the straddling brick is
        duplicated on both edges the seam stays continuous.
      * ``depth_jitter`` -- sub-cm jitter of each brick's depth into the wall (Y).
      * ``tilt_deg`` / ``tilt_frac`` -- a fraction of bricks get a small (<= a few
        degree) random tilt.
      * A flat mortar plane set back ``mortar_depth`` behind the brick faces;
        ``mortar`` (head joint) and ``bed`` (bed joint) are the brick spacing.
      * Romanesque coursing: each brick is the ``main_aspect`` stretcher length
        with probability ``1 - variety``, else a random other length -- so
        courses are a regular running bond of one dominant length with only
        occasional deviations, not a jumble of every size. The course offset is
        half the main stretcher.

    Returns a dict with ``collection``, ``mortar`` object, ``tile_width`` (==
    ``width``) and ``height`` (for downstream cropping/baking)."""
    global _TILT_BIN
    manifest = load_manifest(prefab_dir)
    _TILT_BIN = manifest["tilt_bin"]
    orientations = build_orientations(manifest)
    naspects = len(manifest["aspects"])
    main_aspect = max(0, min(naspects - 1, main_aspect))
    # One side-colour index per aspect (for length-consistent coursing) plus a
    # global index (any aspect) as the match fallback.
    global_index = _index_by_left_colour(orientations)
    aspect_index = [_index_by_left_colour(orientations, aspect=a)
                    for a in range(naspects)]
    nominal_h = manifest["aspects"][0]["height"]
    lengths = [a["length"] for a in manifest["aspects"]]
    min_len = min(lengths)
    main_len = manifest["aspects"][main_aspect]["length"]
    stagger = 0.5 * main_len
    min_stagger = min_stagger_frac * main_len
    tile_width = width
    courses = max(1, int(round(height / (nominal_h + bed))))
    rng = np.random.default_rng(seed)

    if collection is None:
        collection = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(collection)

    mesh_cache = {}
    supp_cache = {}
    baseline = 0.0
    count = 0
    prev_joints = []            # head-joint X positions of the course below
    for c in range(courses):
        # Even courses start on the seam; odd courses straddle it (running bond).
        # ``cursor`` tracks the running X of the last brick's right support face,
        # so the next brick's left face is butted to it with the joint tolerance.
        x_start = 0.0 if (c % 2 == 0) else -stagger
        cursor = x_start        # running right *support face* X
        cursor_b = x_start      # running right *bbox edge* X
        prev = None
        first_tf = None
        placed = []             # this course's instances, in order, for justify
        cur_joints = [x_start]  # this course's head joints (for the next course)
        guard = 0
        # Interior bricks fill up to where the closing brick's left face will sit
        # (one tile on from the first brick's left face).
        limit = x_start + tile_width
        while cursor < limit and guard < 512:
            guard += 1
            # Head-joint tolerance: a few mm, randomised a little per joint.
            tol = 0.0 if prev is None else max(
                0.001, mortar * (1.0 + float(rng.uniform(-0.4, 0.4))))
            # Romanesque coursing: choose the stretcher length so this brick's
            # head joint STAGGERS off the joints in the course below (running
            # bond), mostly the main length with occasional variation. The chosen
            # aspect's colour index is the primary match pool; global_index is the
            # fallback so a match always exists.
            ta = _stagger_aspect(cursor, tol, prev_joints, lengths, main_aspect,
                                 variety, rng, min_stagger)
            oi = _pick_next(orientations, aspect_index[ta], global_index, prev, rng)
            o = orientations[oi]
            mesh = _import_mesh(o["brick"], mesh_cache, prefab_dir)
            dl, dr = _placed_supports(o, mesh, supp_cache)
            # If this brick's right face would cross the closing position, swap
            # it for the longest brick that fits; stop if nothing fits.
            if (cursor + tol) + dl + dr + tol > limit:
                oi = _best_fit(orientations, global_index, prev, cursor + tol, limit)
                if oi is None:
                    break
                o = orientations[oi]
                mesh = _import_mesh(o["brick"], mesh_cache, prefab_dir)
                dl, dr = _placed_supports(o, mesh, supp_cache)
            lo, hi = o["lo"], o["hi"]
            rot = _rand_tilt(rng, tilt_deg, tilt_frac)

            # Butt this brick's left support face to cursor + tolerance...
            loc_support = (cursor + tol) + dl
            # ...but if the support planes leave the actual bboxes further apart
            # than a joint width (e.g. imperfectly matched ends), pull the brick
            # closer so the bbox edges are only `tol` apart -- never a gap.
            loc_bbox = (cursor_b + tol) - lo[0]
            loc_x = loc_support if prev is None else min(loc_support, loc_bbox)
            loc_z = baseline - lo[2] + float(rng.uniform(0.0, seat_jitter))
            loc_y = -0.5 * (lo[1] + hi[1]) + float(
                rng.uniform(-depth_jitter, depth_jitter))
            loc = (loc_x, loc_y, loc_z)
            inst = _place(collection, f"{name}_{c:02d}_{count:04d}", mesh,
                          o["flip"], rot, loc)
            placed.append(inst)
            if first_tf is None:
                first_tf = (mesh, o["flip"], rot, loc)
            cursor = loc_x + dr        # advance right support face
            cursor_b = loc_x + hi[0]   # advance right bbox edge
            cur_joints.append(cursor)  # record this brick's right head joint
            prev = o
            count += 1

        # Justify the course: whatever gap remains between the last brick and the
        # closing position is distributed evenly across every head joint, so the
        # joints just widen slightly instead of leaving a hole before the closer.
        # The first brick stays put (it defines the seam); brick k shifts right by
        # k * slack / m so the last brick meets the closer with one more joint.
        m = len(placed)
        if seamless and m > 0:
            slack = limit - cursor
            if slack > 0.0:
                per = slack / m
                for k, inst in enumerate(placed):
                    inst.location.x += k * per

        # Seamless closer: duplicate the first brick exactly one tile later so
        # the [0, tile_width] crop repeats continuously.
        if seamless and first_tf is not None:
            m0, flip0, rot0, loc0 = first_tf
            loc_end = (loc0[0] + tile_width, loc0[1], loc0[2])
            _place(collection, f"{name}_{c:02d}_{count:04d}_seam", m0,
                   flip0, rot0, loc_end)
            count += 1

        prev_joints = cur_joints   # next course staggers off this course's joints
        baseline += nominal_h + bed

    height = baseline - bed
    front_y = -0.5 * manifest["aspects"][0]["width"]
    mortar_obj = _build_mortar(f"{name}_mortar", 0.0, tile_width, height,
                               front_y, mortar_depth, mortar_cell, collection)

    return {"collection": collection, "mortar": mortar_obj,
            "tile_width": tile_width, "height": height}


if __name__ == "__main__":
    build_wall()
