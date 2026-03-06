"""
Blender Add-on: Export .fskel (Talarium Skeleton Format)

Exports the active armature's bone hierarchy, rest transforms, Blender
bone constraints, and (optionally) inverse-bind matrices to the binary
.fskel format consumed by the Talarium engine.

Install:  Edit → Preferences → Add-ons → Install from Disk → select this file.
Usage:    File → Export → Talarium Skeleton (.fskel)

Compatible with Blender 3.6+ / 4.x.
"""

bl_info = {
    "name": "Talarium Skeleton (.fskel)",
    "author": "Talarium Engine Team",
    "version": (1, 0, 0),
    "blender": (3, 6, 0),
    "location": "File > Export > Talarium Skeleton (.fskel)",
    "description": "Export armature skeleton with constraints to .fskel",
    "category": "Import-Export",
}

import bpy
import struct
import mathutils
import math
from bpy_extras.io_utils import ExportHelper
from bpy.props import StringProperty, BoolProperty

# ── Format constants (must match fskel_format.h) ────────────────────

FSKEL_MAGIC = 0x4C4B5346   # 'FSKL' little-endian
FSKEL_VERSION = 2
SKELETON_JOINT_NAME_MAX = 64

# sizeof(constraint_def_t) = 224 bytes (verified against C compiler)
CONSTRAINT_DEF_SIZE = 224

# ── Constraint type enum (must match constraint_types.h) ────────────

CONSTRAINT_TYPE_MAP = {
    'IK':                   0,   # CONSTRAINT_IK
    'SPLINE_IK':            1,   # CONSTRAINT_SPLINE_IK
    'CHILD_OF':             2,   # CONSTRAINT_CHILD_OF
    'COPY_TRANSFORMS':      3,   # CONSTRAINT_COPY_TRANSFORMS
    'COPY_ROTATION':        4,   # CONSTRAINT_COPY_ROTATION
    'COPY_LOCATION':        5,   # CONSTRAINT_COPY_LOCATION
    'COPY_SCALE':           6,   # CONSTRAINT_COPY_SCALE
    'DAMPED_TRACK':         7,   # CONSTRAINT_DAMPED_TRACK
    'TRACK_TO':             8,   # CONSTRAINT_TRACK_TO
    'LOCKED_TRACK':         9,   # CONSTRAINT_LOCKED_TRACK
    'LIMIT_ROTATION':       10,  # CONSTRAINT_LIMIT_ROTATION
    'LIMIT_LOCATION':       11,  # CONSTRAINT_LIMIT_LOCATION
    'LIMIT_SCALE':          12,  # CONSTRAINT_LIMIT_SCALE
    'TRANSFORMATION':       13,  # CONSTRAINT_TRANSFORMATION
    'ACTION':               14,  # CONSTRAINT_ACTION
    'CLAMP_TO':             15,  # CONSTRAINT_CLAMP_TO
    'FLOOR':                16,  # CONSTRAINT_FLOOR
    'MAINTAIN_VOLUME':      17,  # CONSTRAINT_MAINTAIN_VOLUME
    'SHRINKWRAP':           18,  # CONSTRAINT_SHRINKWRAP
    'PIVOT':                19,  # CONSTRAINT_PIVOT (mapped from PIVOT)
}

# Axis enum (constraint_axis_t)
AXIS_MAP = {
    'TRACK_X':       0,   'POS_X': 0,  'X': 0,
    'TRACK_Y':       1,   'POS_Y': 1,  'Y': 1,
    'TRACK_Z':       2,   'POS_Z': 2,  'Z': 2,
    'TRACK_NEGATIVE_X': 3, 'NEG_X': 3,
    'TRACK_NEGATIVE_Y': 4, 'NEG_Y': 4,
    'TRACK_NEGATIVE_Z': 5, 'NEG_Z': 5,
}

# Mix mode enum (constraint_mix_mode_t)
MIX_MODE_MAP = {
    'REPLACE':     0,
    'BEFORE':      1,
    'AFTER':       2,
    'BEFORE_FULL': 3,
    'AFTER_FULL':  4,
    'MIX':         0,   # Fallback
}

# Space enum (constraint_space_t)
SPACE_MAP = {
    'WORLD':            0,
    'LOCAL':            1,
    'POSE':             2,
    'LOCAL_WITH_PARENT': 1,  # Closest approximation
    'CUSTOM':           0,   # Fallback to world
}

# Floor location enum
FLOOR_LOCATION_MAP = {
    'FLOOR_NEGATIVE_Y': 0,
    'FLOOR_NEGATIVE_X': 1,
    'FLOOR_NEGATIVE_Z': 2,
    'FLOOR_X':          1,  # Fallback
    'FLOOR_Y':          0,
    'FLOOR_Z':          2,
}

# Channel enum (constraint_channel_t)
CHANNEL_MAP = {
    'LOCATION_X': 0, 'LOCATION_Y': 1, 'LOCATION_Z': 2,
    'ROTATION_X': 3, 'ROTATION_Y': 4, 'ROTATION_Z': 5,
    'SCALE_X':    6, 'SCALE_Y':    7, 'SCALE_Z':    8,
}

# Shrinkwrap mode
SHRINKWRAP_MAP = {
    'NEAREST_SURFACEPOINT': 0,
    'PROJECT':              1,
    'NEAREST_VERTEX':       2,
}

# ── Matrix conversion ──────────────────────────────────────────────

# Blender is Z-up, our engine is Y-up.
# Coordinate conversion: (x, y, z)_blender → (x, z, -y)_engine
# As a 4×4 basis-change matrix:
#   C = [[1,0,0,0], [0,0,1,0], [0,-1,0,0], [0,0,0,1]]
# For transforms: M_engine = C × M_blender × C⁻¹
# C⁻¹ = C^T = [[1,0,0,0], [0,0,-1,0], [0,1,0,0], [0,0,0,1]]

def _mat4_mul(a, b):
    """Multiply two 4×4 matrices in row-major list-of-lists form."""
    r = [[0]*4 for _ in range(4)]
    for i in range(4):
        for j in range(4):
            s = 0.0
            for k in range(4):
                s += a[i][k] * b[k][j]
            r[i][j] = s
    return r

# Basis-change matrix and its inverse (row-major, list-of-lists).
_COORD_C = [
    [1,  0, 0, 0],
    [0,  0, 1, 0],
    [0, -1, 0, 0],
    [0,  0, 0, 1],
]
_COORD_C_INV = [
    [1, 0,  0, 0],
    [0, 0, -1, 0],
    [0, 1,  0, 0],
    [0, 0,  0, 1],
]


def blender_to_engine_matrix(bmat):
    """
    Convert a Blender 4×4 matrix to our engine's column-major float[16].

    Applies Z-up → Y-up coordinate conversion: M_engine = C × M_blender × C⁻¹
    Then flattens to column-major for mat4_t.m[16].
    """
    # Convert Blender Matrix to row-major list-of-lists.
    bm = [[bmat[r][c] for c in range(4)] for r in range(4)]
    # Apply basis change: C × M × C⁻¹
    em = _mat4_mul(_COORD_C, _mat4_mul(bm, _COORD_C_INV))
    # Flatten to column-major.
    m = [0.0] * 16
    for col in range(4):
        for row in range(4):
            m[col * 4 + row] = em[row][col]
    return m


def identity_matrix_flat():
    """Return 16-float identity matrix in column-major order."""
    return [
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    ]


# ── Constraint serialization ───────────────────────────────────────

def find_bone_index(bone_name, bone_names):
    """Look up bone index by name. Returns 0xFFFFFFFF if not found."""
    try:
        return bone_names.index(bone_name)
    except ValueError:
        return 0xFFFFFFFF


def pack_constraint_def(con, bone_names):
    """
    Pack a single Blender bone constraint into a 224-byte constraint_def_t.
    
    Layout:
      [0:4]   type (uint32)
      [4:8]   influence (float)
      [8:12]  owner_space (uint32)
      [12:16] target_space (uint32)
      [16:20] target_bone_idx (uint32)
      [20:224] params union (204 bytes)
    """
    buf = bytearray(CONSTRAINT_DEF_SIZE)

    con_type = CONSTRAINT_TYPE_MAP.get(con.type, None)
    if con_type is None:
        return None  # Unsupported constraint type

    # Header fields
    struct.pack_into('<I', buf, 0, con_type)
    struct.pack_into('<f', buf, 4, con.influence)
    struct.pack_into('<I', buf, 8, SPACE_MAP.get(getattr(con, 'owner_space', 'WORLD'), 0))
    struct.pack_into('<I', buf, 12, SPACE_MAP.get(getattr(con, 'target_space', 'WORLD'), 0))

    # Target bone index
    target_bone_idx = 0xFFFFFFFF
    if hasattr(con, 'subtarget') and con.subtarget:
        target_bone_idx = find_bone_index(con.subtarget, bone_names)
    struct.pack_into('<I', buf, 16, target_bone_idx)

    # Params union starts at offset 20
    p = 20

    if con.type == 'IK':
        chain_length = con.chain_count if con.chain_count > 0 else 0
        pole_idx = 0xFFFFFFFF
        if con.pole_subtarget:
            pole_idx = find_bone_index(con.pole_subtarget, bone_names)
        iterations = con.iterations
        weight = con.weight if hasattr(con, 'weight') else 1.0
        orient_weight = getattr(con, 'orient_weight', 0.0)
        use_tail = getattr(con, 'use_tail', True)

        struct.pack_into('<I', buf, p, chain_length)
        struct.pack_into('<I', buf, p + 4, pole_idx)
        struct.pack_into('<I', buf, p + 8, iterations)
        struct.pack_into('<f', buf, p + 12, weight)
        struct.pack_into('<f', buf, p + 16, orient_weight)
        struct.pack_into('<B', buf, p + 20, 1 if use_tail else 0)

    elif con.type == 'SPLINE_IK':
        chain_length = con.chain_count
        struct.pack_into('<I', buf, p, chain_length)
        # Control points from spline (if target is a curve)
        cp_offset = p + 4
        cp_count = 0
        if con.target and con.target.type == 'CURVE':
            spline = con.target.data.splines[0] if con.target.data.splines else None
            if spline:
                for i, pt in enumerate(spline.points[:16]):
                    co = pt.co
                    struct.pack_into('<fff', buf, cp_offset + i * 12,
                                    co[0], co[1], co[2])
                    cp_count += 1
        struct.pack_into('<I', buf, cp_offset + 16 * 12, cp_count)

    elif con.type == 'CHILD_OF':
        # Booleans for which channels to use
        struct.pack_into('<B', buf, p + 0, 1 if con.use_location_x else 0)
        struct.pack_into('<B', buf, p + 1, 1 if con.use_location_y else 0)
        struct.pack_into('<B', buf, p + 2, 1 if con.use_location_z else 0)
        struct.pack_into('<B', buf, p + 3, 1 if con.use_rotation_x else 0)
        struct.pack_into('<B', buf, p + 4, 1 if con.use_rotation_y else 0)
        struct.pack_into('<B', buf, p + 5, 1 if con.use_rotation_z else 0)
        struct.pack_into('<B', buf, p + 6, 1 if con.use_scale_x else 0)
        struct.pack_into('<B', buf, p + 7, 1 if con.use_scale_y else 0)
        struct.pack_into('<B', buf, p + 8, 1 if con.use_scale_z else 0)
        # inverse_matrix (mat4_t = 64 bytes, column-major)
        inv = blender_to_engine_matrix(con.inverse_matrix)
        for i, v in enumerate(inv):
            struct.pack_into('<f', buf, p + 12 + i * 4, v)

    elif con.type == 'COPY_TRANSFORMS':
        mix = MIX_MODE_MAP.get(getattr(con, 'mix_mode', 'REPLACE'), 0)
        struct.pack_into('<I', buf, p, mix)

    elif con.type == 'COPY_ROTATION':
        mix = MIX_MODE_MAP.get(getattr(con, 'mix_mode', 'REPLACE'), 0)
        struct.pack_into('<I', buf, p, mix)
        struct.pack_into('<B', buf, p + 4, 1 if con.use_x else 0)
        struct.pack_into('<B', buf, p + 5, 1 if con.use_y else 0)
        struct.pack_into('<B', buf, p + 6, 1 if con.use_z else 0)
        struct.pack_into('<B', buf, p + 7, 1 if con.invert_x else 0)
        struct.pack_into('<B', buf, p + 8, 1 if con.invert_y else 0)
        struct.pack_into('<B', buf, p + 9, 1 if con.invert_z else 0)

    elif con.type == 'COPY_LOCATION':
        struct.pack_into('<B', buf, p + 0, 1 if con.use_x else 0)
        struct.pack_into('<B', buf, p + 1, 1 if con.use_y else 0)
        struct.pack_into('<B', buf, p + 2, 1 if con.use_z else 0)
        struct.pack_into('<B', buf, p + 3, 1 if con.invert_x else 0)
        struct.pack_into('<B', buf, p + 4, 1 if con.invert_y else 0)
        struct.pack_into('<B', buf, p + 5, 1 if con.invert_z else 0)
        struct.pack_into('<B', buf, p + 6, 1 if con.use_offset else 0)

    elif con.type == 'COPY_SCALE':
        struct.pack_into('<B', buf, p + 0, 1 if con.use_x else 0)
        struct.pack_into('<B', buf, p + 1, 1 if con.use_y else 0)
        struct.pack_into('<B', buf, p + 2, 1 if con.use_z else 0)
        struct.pack_into('<f', buf, p + 4, getattr(con, 'power', 1.0))
        struct.pack_into('<B', buf, p + 8, 1 if con.use_offset else 0)

    elif con.type == 'DAMPED_TRACK':
        axis = AXIS_MAP.get(con.track_axis, 1)
        struct.pack_into('<I', buf, p, axis)

    elif con.type == 'TRACK_TO':
        track = AXIS_MAP.get(con.track_axis, 1)
        up = AXIS_MAP.get(con.up_axis, 2)
        struct.pack_into('<I', buf, p, track)
        struct.pack_into('<I', buf, p + 4, up)

    elif con.type == 'LOCKED_TRACK':
        track = AXIS_MAP.get(con.track_axis, 1)
        lock = AXIS_MAP.get(con.lock_axis, 2)
        struct.pack_into('<I', buf, p, track)
        struct.pack_into('<I', buf, p + 4, lock)

    elif con.type == 'LIMIT_ROTATION':
        struct.pack_into('<f', buf, p + 0, con.min_x)
        struct.pack_into('<f', buf, p + 4, con.max_x)
        struct.pack_into('<f', buf, p + 8, con.min_y)
        struct.pack_into('<f', buf, p + 12, con.max_y)
        struct.pack_into('<f', buf, p + 16, con.min_z)
        struct.pack_into('<f', buf, p + 20, con.max_z)
        struct.pack_into('<B', buf, p + 24, 1 if con.use_limit_x else 0)
        struct.pack_into('<B', buf, p + 25, 1 if con.use_limit_y else 0)
        struct.pack_into('<B', buf, p + 26, 1 if con.use_limit_z else 0)

    elif con.type == 'LIMIT_LOCATION':
        struct.pack_into('<f', buf, p + 0, con.min_x)
        struct.pack_into('<f', buf, p + 4, con.max_x)
        struct.pack_into('<f', buf, p + 8, con.min_y)
        struct.pack_into('<f', buf, p + 12, con.max_y)
        struct.pack_into('<f', buf, p + 16, con.min_z)
        struct.pack_into('<f', buf, p + 20, con.max_z)
        struct.pack_into('<B', buf, p + 24, 1 if con.use_min_x else 0)
        struct.pack_into('<B', buf, p + 25, 1 if con.use_max_x else 0)
        struct.pack_into('<B', buf, p + 26, 1 if con.use_min_y else 0)
        struct.pack_into('<B', buf, p + 27, 1 if con.use_max_y else 0)
        struct.pack_into('<B', buf, p + 28, 1 if con.use_min_z else 0)
        struct.pack_into('<B', buf, p + 29, 1 if con.use_max_z else 0)

    elif con.type == 'LIMIT_SCALE':
        struct.pack_into('<f', buf, p + 0, con.min_x)
        struct.pack_into('<f', buf, p + 4, con.max_x)
        struct.pack_into('<f', buf, p + 8, con.min_y)
        struct.pack_into('<f', buf, p + 12, con.max_y)
        struct.pack_into('<f', buf, p + 16, con.min_z)
        struct.pack_into('<f', buf, p + 20, con.max_z)
        struct.pack_into('<B', buf, p + 24, 1 if con.use_min_x else 0)
        struct.pack_into('<B', buf, p + 25, 1 if con.use_max_x else 0)
        struct.pack_into('<B', buf, p + 26, 1 if con.use_min_y else 0)
        struct.pack_into('<B', buf, p + 27, 1 if con.use_max_y else 0)
        struct.pack_into('<B', buf, p + 28, 1 if con.use_min_z else 0)
        struct.pack_into('<B', buf, p + 29, 1 if con.use_max_z else 0)

    elif con.type == 'TRANSFORMATION':
        from_ch = CHANNEL_MAP.get(getattr(con, 'map_from', 'LOCATION_X'), 0)
        to_ch = CHANNEL_MAP.get(getattr(con, 'map_to', 'LOCATION_X'), 0)
        struct.pack_into('<I', buf, p + 0, from_ch)
        struct.pack_into('<I', buf, p + 4, to_ch)
        struct.pack_into('<f', buf, p + 8, getattr(con, 'from_min_x', 0.0))
        struct.pack_into('<f', buf, p + 12, getattr(con, 'from_max_x', 1.0))
        struct.pack_into('<f', buf, p + 16, getattr(con, 'to_min_x', 0.0))
        struct.pack_into('<f', buf, p + 20, getattr(con, 'to_max_x', 1.0))
        struct.pack_into('<B', buf, p + 24,
                         1 if getattr(con, 'use_motion_extrapolate', False) else 0)

    elif con.type == 'ACTION':
        struct.pack_into('<I', buf, p + 0, 0)  # action_clip_idx (not mapped here)
        # Try to map transform_channel
        ch = CHANNEL_MAP.get(getattr(con, 'transform_channel', 'LOCATION_X'), 0)
        struct.pack_into('<I', buf, p + 4, ch)
        struct.pack_into('<f', buf, p + 8, getattr(con, 'min', 0.0))
        struct.pack_into('<f', buf, p + 12, getattr(con, 'max', 1.0))

    elif con.type == 'CLAMP_TO':
        axis = AXIS_MAP.get(getattr(con, 'main_axis', 'X'), 0)
        struct.pack_into('<I', buf, p, axis)
        # Control points from target curve
        cp_offset = p + 4
        cp_count = 0
        if con.target and con.target.type == 'CURVE':
            spline = con.target.data.splines[0] if con.target.data.splines else None
            if spline:
                for i, pt in enumerate(spline.points[:16]):
                    co = pt.co
                    struct.pack_into('<fff', buf, cp_offset + i * 12,
                                    co[0], co[1], co[2])
                    cp_count += 1
        struct.pack_into('<I', buf, cp_offset + 16 * 12, cp_count)
        struct.pack_into('<B', buf, cp_offset + 16 * 12 + 4,
                         1 if getattr(con, 'use_cyclic', False) else 0)

    elif con.type == 'FLOOR':
        struct.pack_into('<f', buf, p + 0, con.offset)
        struct.pack_into('<B', buf, p + 4, 1 if con.use_rotation else 0)
        floor_loc = FLOOR_LOCATION_MAP.get(con.floor_location, 0)
        struct.pack_into('<I', buf, p + 8, floor_loc)

    elif con.type == 'MAINTAIN_VOLUME':
        axis = AXIS_MAP.get(con.free_axis, 1)
        struct.pack_into('<I', buf, p + 0, axis)
        struct.pack_into('<f', buf, p + 4, con.volume if hasattr(con, 'volume') else 1.0)

    elif con.type == 'SHRINKWRAP':
        mode = SHRINKWRAP_MAP.get(getattr(con, 'shrinkwrap_type', 'NEAREST_SURFACEPOINT'), 0)
        struct.pack_into('<I', buf, p + 0, mode)
        struct.pack_into('<f', buf, p + 4, con.distance)

    elif con.type == 'PIVOT':
        offset = getattr(con, 'offset', (0, 0, 0))
        struct.pack_into('<f', buf, p + 0, offset[0])
        struct.pack_into('<f', buf, p + 4, offset[1])
        struct.pack_into('<f', buf, p + 8, offset[2])
        struct.pack_into('<f', buf, p + 12,
                         getattr(con, 'rotation_range', 0.0))

    return bytes(buf)


# ── Collision hull helpers ──────────────────────────────────────────

def _gather_hull_vertices(armature_obj, bone_name, vgroup_name):
    """
    Gather world-space vertices from a vertex group on a mesh parented
    to the armature.  Returns flat list [x0,y0,z0, x1,y1,z1, ...] in
    engine coordinate space.  Returns empty list if no vertices found.
    """
    verts = []
    if not vgroup_name:
        return verts
    # Search child meshes for vertex group
    for child in armature_obj.children:
        if child.type != 'MESH':
            continue
        vg = child.vertex_groups.get(vgroup_name)
        if vg is None:
            continue
        vg_idx = vg.index
        mesh = child.data
        for v in mesh.vertices:
            for g in v.groups:
                if g.group == vg_idx and g.weight > 0.5:
                    # Transform to world, then convert coords
                    co = child.matrix_world @ v.co
                    # Blender Z-up → engine Y-up: (x,y,z) → (x,z,-y)
                    verts.extend([co.x, co.z, -co.y])
                    break
    return verts


# ── Main export logic ──────────────────────────────────────────────

def export_fskel(context, filepath, export_ibms):
    """
    Export the active armature to .fskel format.
    
    Returns set of {'FINISHED'} or {'CANCELLED'}.
    """
    obj = context.active_object
    if not obj or obj.type != 'ARMATURE':
        # Try to find first armature in scene
        for o in context.scene.objects:
            if o.type == 'ARMATURE':
                obj = o
                break
    if not obj or obj.type != 'ARMATURE':
        raise RuntimeError("No armature found in scene")

    armature = obj.data
    bones = armature.bones

    # Build ordered bone list (depth-first to match Blender ordering)
    bone_list = list(bones)
    bone_names = [b.name for b in bone_list]
    joint_count = len(bone_list)

    if joint_count == 0:
        raise RuntimeError("Armature has no bones")

    # Gather constraints per bone from pose bones
    pose_bones = obj.pose.bones
    max_constraints = 0
    bone_constraints = []  # List of lists of constraint bytes

    for bone in bone_list:
        pb = pose_bones.get(bone.name)
        cons = []
        if pb:
            for c in pb.constraints:
                if not c.mute:  # Skip muted constraints
                    packed = pack_constraint_def(c, bone_names)
                    if packed is not None:
                        cons.append(packed)
        bone_constraints.append(cons)
        if len(cons) > max_constraints:
            max_constraints = len(cons)

    # Ensure at least 1 for the constraint array allocation
    if max_constraints == 0:
        max_constraints = 1

    # Compute inverse bind matrices if requested
    ibms = []
    if export_ibms:
        for bone in bone_list:
            # IBM = inverse of (armature_world × bone_rest_world)
            bone_world = obj.matrix_world @ bone.matrix_local
            ibm = bone_world.inverted_safe()
            ibms.append(blender_to_engine_matrix(ibm))

    ibm_count = len(ibms)

    # ── Write binary file ───────────────────────────────────────

    with open(filepath, 'wb') as f:
        # Header: magic, version, joint_count, max_constraints, ibm_count
        f.write(struct.pack('<IIIII',
                            FSKEL_MAGIC, FSKEL_VERSION,
                            joint_count, max_constraints, ibm_count))

        # Joint names (64 bytes each, null-padded)
        for name in bone_names:
            encoded = name.encode('utf-8')[:SKELETON_JOINT_NAME_MAX - 1]
            padded = encoded + b'\x00' * (SKELETON_JOINT_NAME_MAX - len(encoded))
            f.write(padded)

        # Parent indices (uint32 each, UINT32_MAX for roots)
        for bone in bone_list:
            if bone.parent:
                idx = find_bone_index(bone.parent.name, bone_names)
            else:
                idx = 0xFFFFFFFF
            f.write(struct.pack('<I', idx))

        # Rest local transforms (mat4, 64 bytes each)
        for bone in bone_list:
            if bone.parent:
                local = bone.parent.matrix_local.inverted_safe() @ bone.matrix_local
            else:
                local = bone.matrix_local
            flat = blender_to_engine_matrix(local)
            f.write(struct.pack('<16f', *flat))

        # Rest world transforms (mat4, 64 bytes each)
        for bone in bone_list:
            flat = blender_to_engine_matrix(bone.matrix_local)
            f.write(struct.pack('<16f', *flat))

        # Constraint counts (uint32 each)
        for cons in bone_constraints:
            f.write(struct.pack('<I', len(cons)))

        # Constraints (flat: joint_count × max_constraints × 224 bytes)
        for cons in bone_constraints:
            for c_bytes in cons:
                f.write(c_bytes)
            # Pad remaining slots with zeros
            remaining = max_constraints - len(cons)
            f.write(b'\x00' * (remaining * CONSTRAINT_DEF_SIZE))

        # Inverse bind matrices (mat4, 64 bytes each)
        for ibm in ibms:
            f.write(struct.pack('<16f', *ibm))

        # --- v2 COLL chunk: per-bone collision descriptors ---
        # Gather hull vertex data across all bones.
        hull_vertices = []  # Flat list of (x, y, z) floats
        bone_collider_descs = []

        for bone in bone_list:
            pb = pose_bones.get(bone.name)

            shape_type = 0  # NONE
            params = [0.0] * 6
            ccd_enabled = 0
            is_kinematic = 0
            mass = 0.0
            hull_offset = 0
            hull_count = 0

            if pb:
                shape_type = int(pb.get('talarium_collision_shape', 0))
                ccd_enabled = int(pb.get('talarium_ccd', 0))
                is_kinematic = int(pb.get('talarium_kinematic', 0))
                mass = float(pb.get('talarium_mass', 0.0))

                if shape_type == 1:  # Capsule
                    params[0] = float(pb.get('talarium_capsule_radius', 0.05))
                    params[1] = float(pb.get('talarium_capsule_height', 0.2))
                    axis_name = pb.get('talarium_capsule_axis', 'Y')
                    params[2] = float({'X': 0, 'Y': 1, 'Z': 2}.get(
                        str(axis_name), 1))
                elif shape_type == 2:  # Box
                    params[0] = float(pb.get('talarium_box_hx', 0.1))
                    params[1] = float(pb.get('talarium_box_hy', 0.1))
                    params[2] = float(pb.get('talarium_box_hz', 0.1))
                elif shape_type == 3:  # Sphere
                    params[0] = float(pb.get('talarium_sphere_radius', 0.1))
                elif shape_type == 4:  # Convex hull
                    hull_offset = len(hull_vertices) // 3
                    vg_name = str(pb.get('talarium_hull_vgroup', ''))
                    # Find mesh vertices in vertex group
                    hull_verts = _gather_hull_vertices(
                        obj, bone.name, vg_name)
                    hull_count = len(hull_verts)
                    hull_vertices.extend(hull_verts)

            # sizeof(bone_collider_desc_t) = 4 + 24 + 4 + 4 + 4 + 4 + 4 = 48
            desc = struct.pack('<I6fIIfII',
                               shape_type,
                               params[0], params[1], params[2],
                               params[3], params[4], params[5],
                               ccd_enabled, is_kinematic, mass,
                               hull_offset, hull_count)
            bone_collider_descs.append(desc)

        hull_vertex_count = len(hull_vertices) // 3
        f.write(struct.pack('<I', hull_vertex_count))

        for desc in bone_collider_descs:
            f.write(desc)

        # Hull vertex data (float x,y,z triples)
        for i in range(0, len(hull_vertices), 3):
            f.write(struct.pack('<fff',
                                hull_vertices[i],
                                hull_vertices[i + 1],
                                hull_vertices[i + 2]))

    # Summary
    total_constraints = sum(len(c) for c in bone_constraints)
    total_colliders = sum(1 for d in bone_collider_descs
                          if struct.unpack_from('<I', d, 0)[0] != 0)
    print(f"Exported {filepath}:")
    print(f"  {joint_count} joints, {total_constraints} constraints, "
          f"{ibm_count} IBMs, {total_colliders} colliders, "
          f"{hull_vertex_count} hull vertices")
    print(f"  max_constraints_per_joint = {max_constraints}")

    return {'FINISHED'}


# ── Blender operator ──────────────────────────────────────────────

class ExportFSKEL(bpy.types.Operator, ExportHelper):
    """Export active armature to Talarium .fskel format"""
    bl_idname = "export_anim.fskel"
    bl_label = "Export .fskel"
    bl_options = {'PRESET'}

    filename_ext = ".fskel"

    filter_glob: StringProperty(
        default="*.fskel",
        options={'HIDDEN'},
        maxlen=255,
    )

    export_ibms: BoolProperty(
        name="Export Inverse Bind Matrices",
        description="Include inverse bind matrices for GPU skinning",
        default=True,
    )

    def execute(self, context):
        try:
            result = export_fskel(context, self.filepath, self.export_ibms)
            self.report({'INFO'}, f"Exported to {self.filepath}")
            return result
        except RuntimeError as e:
            self.report({'ERROR'}, str(e))
            return {'CANCELLED'}
        except Exception as e:
            self.report({'ERROR'}, f"Export failed: {e}")
            return {'CANCELLED'}

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "export_ibms")


# ── Registration ──────────────────────────────────────────────────

def menu_func_export(self, context):
    self.layout.operator(ExportFSKEL.bl_idname,
                         text="Talarium Skeleton (.fskel)")


def register():
    bpy.utils.register_class(ExportFSKEL)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    bpy.utils.unregister_class(ExportFSKEL)


if __name__ == "__main__":
    register()
