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
import gpu
from gpu_extras.batch import batch_for_shader
from bpy_extras.io_utils import ExportHelper
from bpy.props import StringProperty, BoolProperty, EnumProperty, FloatProperty, IntProperty

# ── Format constants (must match fskel_format.h) ────────────────────

FSKEL_MAGIC = 0x4C4B5346   # 'FSKL' little-endian
FSKEL_VERSION = 4
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

def export_fskel(context, filepath, export_ibms, default_collision='EMPTY'):
    """
    Export the active armature to .fskel format.
    
    Args:
        default_collision: How to handle bones without explicit collision
            geometry.  'EMPTY' = no collider, 'ENVELOPE' = capsule from
            bone envelope (head/tail radius and length).
    
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
            collision_group = 0

            if pb:
                shape_type = int(pb.talarium_collision_shape)
                ccd_enabled = 1 if pb.talarium_ccd else 0
                is_kinematic = 1 if pb.talarium_kinematic else 0
                mass = pb.talarium_mass
                collision_group = pb.talarium_collision_group
                explicit_shape = shape_type  # Original user-set value.

                if shape_type == 1:  # Capsule (explicit)
                    params[0] = pb.talarium_capsule_radius
                    params[1] = pb.talarium_capsule_height
                    params[2] = float({'X': 0, 'Y': 1, 'Z': 2}.get(
                        pb.talarium_capsule_axis, 1))
                elif shape_type == 2:  # Box
                    params[0] = pb.talarium_box_hx
                    params[1] = pb.talarium_box_hy
                    params[2] = pb.talarium_box_hz
                elif shape_type == 3:  # Sphere
                    params[0] = pb.talarium_sphere_radius
                elif shape_type == 4:  # Convex hull
                    hull_offset = len(hull_vertices) // 3
                    vg_name = pb.talarium_hull_vgroup
                    hull_verts = _gather_hull_vertices(
                        obj, bone.name, vg_name)
                    hull_count = len(hull_verts)
                    hull_vertices.extend(hull_verts)
                elif explicit_shape == 0 and default_collision == 'ENVELOPE':
                    # Auto-generate capsule from bone envelope.
                    bone_len = bone.length
                    if bone_len > 1e-6:
                        avg_radius = (bone.head_radius + bone.tail_radius) * 0.5
                        if avg_radius < 1e-6:
                            avg_radius = bone_len * 0.1
                        shape_type = 1  # Capsule
                        params[0] = avg_radius
                        params[1] = bone_len
                        # Blender bone Y axis → engine Y axis after coord
                        # conversion.  Capsule axis enum: 0=X, 1=Y, 2=Z.
                        params[2] = 1.0

            # sizeof(bone_collider_desc_t) = 4 + 24 + 4 + 4 + 4 + 4 + 4 + 4 = 52
            desc = struct.pack('<I6fIIfIII',
                               shape_type,
                               params[0], params[1], params[2],
                               params[3], params[4], params[5],
                               ccd_enabled, is_kinematic, mass,
                               hull_offset, hull_count, collision_group)
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

        # --- v3 JNTS chunk: per-bone joint descriptors ---
        # sizeof(bone_joint_desc_t) = 4 + 12 + 4 + 12 + 12 + 4 = 48 bytes
        # joint_type(I) axis(3f) rest_length(f) limit_min(3f) limit_max(3f) limit_axes(I)
        for bone in bone_list:
            pb = pose_bones.get(bone.name)

            jt = 0  # NONE
            axis = [0.0, 1.0, 0.0]
            rest_len = 0.0
            lim_min = [0.0, 0.0, 0.0]
            lim_max = [0.0, 0.0, 0.0]
            lim_axes = 0

            if pb and bone.parent:
                jt = int(pb.talarium_joint_type)

                if jt in (2, 8):  # Hinge or Aim: use axis
                    axis_str = pb.talarium_joint_axis
                    axis = {'X': [1, 0, 0], 'Y': [0, 1, 0],
                            'Z': [0, 0, 1]}.get(axis_str, [0, 1, 0])
                    # Convert axis from Blender Z-up to engine Y-up
                    bx, by, bz = axis
                    axis = [bx, bz, -by]

                if jt == 2:  # Hinge: scalar limits in slot [0]
                    lim_min[0] = pb.talarium_joint_limit_min
                    lim_max[0] = pb.talarium_joint_limit_max
                    if lim_min[0] != 0.0 or lim_max[0] != 0.0:
                        lim_axes = 1

                elif jt == 3:  # Distance
                    rest_len = pb.talarium_joint_rest_length

                elif jt == 6:  # Limit rotation: per-axis angle limits
                    # Read Blender-space per-axis limits
                    bl_min_x = pb.talarium_joint_limit_min_x
                    bl_max_x = pb.talarium_joint_limit_max_x
                    bl_min_y = pb.talarium_joint_limit_min_y
                    bl_max_y = pb.talarium_joint_limit_max_y
                    bl_min_z = pb.talarium_joint_limit_min_z
                    bl_max_z = pb.talarium_joint_limit_max_z
                    bl_use_x = pb.talarium_joint_use_limit_x
                    bl_use_y = pb.talarium_joint_use_limit_y
                    bl_use_z = pb.talarium_joint_use_limit_z
                    # Coordinate conversion: Blender Z-up → engine Y-up
                    # Blender X → engine X, Blender Z → engine Y,
                    # Blender Y → engine -Z (negate + swap min/max)
                    lim_min[0] = bl_min_x
                    lim_max[0] = bl_max_x
                    lim_min[1] = bl_min_z
                    lim_max[1] = bl_max_z
                    lim_min[2] = -bl_max_y
                    lim_max[2] = -bl_min_y
                    lim_axes = (bl_use_x) | (bl_use_z << 1) | (bl_use_y << 2)

                elif jt == 7:  # Limit position: per-axis position limits
                    bl_min_x = pb.talarium_joint_limit_min_x
                    bl_max_x = pb.talarium_joint_limit_max_x
                    bl_min_y = pb.talarium_joint_limit_min_y
                    bl_max_y = pb.talarium_joint_limit_max_y
                    bl_min_z = pb.talarium_joint_limit_min_z
                    bl_max_z = pb.talarium_joint_limit_max_z
                    bl_use_x = pb.talarium_joint_use_limit_x
                    bl_use_y = pb.talarium_joint_use_limit_y
                    bl_use_z = pb.talarium_joint_use_limit_z
                    # Same coordinate conversion as rotation limits
                    lim_min[0] = bl_min_x
                    lim_max[0] = bl_max_x
                    lim_min[1] = bl_min_z
                    lim_max[1] = bl_max_z
                    lim_min[2] = -bl_max_y
                    lim_max[2] = -bl_min_y
                    lim_axes = (bl_use_x) | (bl_use_z << 1) | (bl_use_y << 2)

            f.write(struct.pack('<I3ff3f3fI', jt,
                                axis[0], axis[1], axis[2],
                                rest_len,
                                lim_min[0], lim_min[1], lim_min[2],
                                lim_max[0], lim_max[1], lim_max[2],
                                lim_axes))

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

    default_collision: EnumProperty(
        name="Default Collision",
        description="Collision geometry for bones without explicit custom shapes",
        items=[
            ('EMPTY', "Empty (None)",
             "Bones without custom shapes get no collider"),
            ('ENVELOPE', "Capsule from Envelope",
             "Auto-generate capsule colliders from bone envelope "
             "(head/tail radius and length)"),
        ],
        default='EMPTY',
    )

    def execute(self, context):
        try:
            result = export_fskel(context, self.filepath,
                                  self.export_ibms, self.default_collision)
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
        layout.prop(self, "default_collision")


# ── Per-bone custom properties ────────────────────────────────────

# These are registered on PoseBone so they appear in the bone
# properties panel and are saved with the .blend file.

_BONE_PROPS = {
    # Collision shape
    "talarium_collision_shape": EnumProperty(
        name="Shape",
        description="Collision geometry for this bone",
        items=[
            ('0', "None", "No collision geometry"),
            ('1', "Capsule", "Capsule collider (radius + height)"),
            ('2', "Box", "Box collider (half-extents)"),
            ('3', "Sphere", "Sphere collider (radius)"),
            ('4', "Convex Hull", "Convex hull from vertex group"),
        ],
        default='0',
    ),

    # Physics flags
    "talarium_kinematic": BoolProperty(
        name="Kinematic",
        description="Skip Euler-Verlet integration (animation-only, "
                    "no external forces)",
        default=False,
    ),
    "talarium_ccd": BoolProperty(
        name="CCD",
        description="Enable continuous collision detection for this bone",
        default=False,
    ),
    "talarium_mass": FloatProperty(
        name="Mass",
        description="Mass override (0 = auto from volume × density, "
                    "ignored if kinematic)",
        default=0.0,
        min=0.0,
        soft_max=100.0,
        unit='MASS',
    ),

    # Capsule params
    "talarium_capsule_radius": FloatProperty(
        name="Radius", description="Capsule radius",
        default=0.05, min=0.001, soft_max=2.0, unit='LENGTH',
    ),
    "talarium_capsule_height": FloatProperty(
        name="Height", description="Capsule height (end-to-end)",
        default=0.2, min=0.001, soft_max=5.0, unit='LENGTH',
    ),
    "talarium_capsule_axis": EnumProperty(
        name="Axis", description="Capsule alignment axis",
        items=[('X', "X", ""), ('Y', "Y", ""), ('Z', "Z", "")],
        default='Y',
    ),

    # Box params
    "talarium_box_hx": FloatProperty(
        name="Half X", description="Box half-extent X",
        default=0.1, min=0.001, soft_max=5.0, unit='LENGTH',
    ),
    "talarium_box_hy": FloatProperty(
        name="Half Y", description="Box half-extent Y",
        default=0.1, min=0.001, soft_max=5.0, unit='LENGTH',
    ),
    "talarium_box_hz": FloatProperty(
        name="Half Z", description="Box half-extent Z",
        default=0.1, min=0.001, soft_max=5.0, unit='LENGTH',
    ),

    # Sphere params
    "talarium_sphere_radius": FloatProperty(
        name="Radius", description="Sphere radius",
        default=0.1, min=0.001, soft_max=5.0, unit='LENGTH',
    ),

    # Convex hull params
    "talarium_hull_vgroup": StringProperty(
        name="Vertex Group",
        description="Name of vertex group whose vertices define the "
                    "convex hull",
        default="",
    ),

    # Collision group
    "talarium_collision_group": IntProperty(
        name="Collision Group",
        description="Bones in the same non-zero group skip collision "
                    "with each other.  0 = no group filtering (default)",
        default=0,
        min=0,
        soft_max=16,
    ),

    # Joint type
    "talarium_joint_type": EnumProperty(
        name="Joint",
        description="Physics joint type for parent-child connection",
        items=[
            ('0', "None", "No joint"),
            ('1', "Ball", "Ball-and-socket joint (3 DOF)"),
            ('2', "Hinge", "Hinge joint (1 DOF rotation)"),
            ('3', "Distance", "Distance constraint"),
            ('4', "Lock", "Fully locked (0 DOF)"),
            ('5', "Copy Rotation", "Copy parent rotation"),
            ('6', "Limit Rotation", "Rotation with per-axis limits"),
            ('7', "Limit Position", "Position with per-axis limits"),
            ('8', "Aim", "Aim/track constraint"),
        ],
        default='1',
    ),

    # Joint axis (for hinge / aim)
    "talarium_joint_axis": EnumProperty(
        name="Axis", description="Joint rotation or aim axis",
        items=[('X', "X", ""), ('Y', "Y", ""), ('Z', "Z", "")],
        default='Y',
    ),

    # Joint limits (scalar for hinge, per-axis for limit types)
    "talarium_joint_limit_min": FloatProperty(
        name="Limit Min", description="Hinge lower limit (radians)",
        default=0.0, subtype='ANGLE',
    ),
    "talarium_joint_limit_max": FloatProperty(
        name="Limit Max", description="Hinge upper limit (radians)",
        default=0.0, subtype='ANGLE',
    ),
    "talarium_joint_rest_length": FloatProperty(
        name="Rest Length", description="Distance joint rest length "
                                        "(0 = auto from bone length)",
        default=0.0, min=0.0, unit='LENGTH',
    ),

    # Per-axis limits (for limit_rotation / limit_position)
    "talarium_joint_limit_min_x": FloatProperty(
        name="Min X", default=0.0, subtype='ANGLE'),
    "talarium_joint_limit_max_x": FloatProperty(
        name="Max X", default=0.0, subtype='ANGLE'),
    "talarium_joint_limit_min_y": FloatProperty(
        name="Min Y", default=0.0, subtype='ANGLE'),
    "talarium_joint_limit_max_y": FloatProperty(
        name="Max Y", default=0.0, subtype='ANGLE'),
    "talarium_joint_limit_min_z": FloatProperty(
        name="Min Z", default=0.0, subtype='ANGLE'),
    "talarium_joint_limit_max_z": FloatProperty(
        name="Max Z", default=0.0, subtype='ANGLE'),
    "talarium_joint_use_limit_x": BoolProperty(
        name="Use X", default=False),
    "talarium_joint_use_limit_y": BoolProperty(
        name="Use Y", default=False),
    "talarium_joint_use_limit_z": BoolProperty(
        name="Use Z", default=False),
}


# ── Operators for auto-filling collision defaults ──────────────────

class BONE_OT_talarium_autofill_collision(bpy.types.Operator):
    """Auto-fill collision shape defaults from bone geometry"""
    bl_idname = "bone.talarium_autofill_collision"
    bl_label = "Auto-fill from Bone"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        return context.active_pose_bone is not None

    def execute(self, context):
        pb = context.active_pose_bone
        bone = pb.bone
        shape = pb.talarium_collision_shape

        if shape == '1':  # Capsule: defaults from bone envelope
            avg_r = (bone.head_radius + bone.tail_radius) * 0.5
            if avg_r < 1e-6:
                avg_r = bone.length * 0.1
            pb.talarium_capsule_radius = avg_r
            pb.talarium_capsule_height = bone.length
            # Bone local Y axis = bone direction
            pb.talarium_capsule_axis = 'Y'
            self.report({'INFO'}, f"Capsule: r={avg_r:.4f} h={bone.length:.4f}")

        elif shape == '2':  # Box: OBB from vertex group
            obj = context.active_object
            mesh_obj = _find_mesh_for_armature(obj)
            if mesh_obj and mesh_obj.type == 'MESH':
                he = _compute_bone_obb(mesh_obj, bone)
                if he:
                    pb.talarium_box_hx = he[0]
                    pb.talarium_box_hy = he[1]
                    pb.talarium_box_hz = he[2]
                    self.report({'INFO'},
                                f"Box: hx={he[0]:.4f} hy={he[1]:.4f} hz={he[2]:.4f}")
                else:
                    self.report({'WARNING'},
                                f"No vertex group '{bone.name}' found")
            else:
                # Fallback: use bone length to estimate box
                hl = bone.length * 0.5
                hr = bone.length * 0.15
                pb.talarium_box_hx = hr
                pb.talarium_box_hy = hl
                pb.talarium_box_hz = hr
                self.report({'INFO'}, "Box estimated from bone length")

        elif shape == '3':  # Sphere: from bone envelope
            avg_r = (bone.head_radius + bone.tail_radius) * 0.5
            if avg_r < 1e-6:
                avg_r = bone.length * 0.2
            pb.talarium_sphere_radius = avg_r
            self.report({'INFO'}, f"Sphere: r={avg_r:.4f}")

        elif shape == '4':  # Convex Hull: default vertex group
            if not pb.talarium_hull_vgroup:
                pb.talarium_hull_vgroup = bone.name
                self.report({'INFO'},
                            f"Hull vertex group set to '{bone.name}'")
        else:
            self.report({'WARNING'}, "Select a collision shape first")

        return {'FINISHED'}


def _find_mesh_for_armature(armature_obj):
    """Find the first mesh child of the armature."""
    for child in armature_obj.children:
        if child.type == 'MESH':
            return child
    return None


def _compute_bone_obb(mesh_obj, bone):
    """Compute OBB half-extents for a bone's vertex group in bone-local space.

    Returns (half_x, half_y, half_z) or None if vertex group not found.
    """
    vg = mesh_obj.vertex_groups.get(bone.name)
    if not vg:
        return None

    vg_idx = vg.index
    mesh = mesh_obj.data

    # Collect vertices in this group
    coords = []
    for v in mesh.vertices:
        for g in v.groups:
            if g.group == vg_idx and g.weight > 0.1:
                coords.append(v.co.copy())
                break

    if len(coords) < 3:
        return None

    # Transform to bone-local space
    # Bone rest matrix: world-space of the bone in rest pose
    bone_mat = armature_obj_matrix(mesh_obj) @ bone.matrix_local
    bone_inv = bone_mat.inverted_safe()

    local_coords = [bone_inv @ co for co in coords]

    # Compute AABB in bone-local space (approximating OBB)
    min_v = local_coords[0].copy()
    max_v = local_coords[0].copy()
    for co in local_coords[1:]:
        for i in range(3):
            if co[i] < min_v[i]:
                min_v[i] = co[i]
            if co[i] > max_v[i]:
                max_v[i] = co[i]

    half_x = (max_v[0] - min_v[0]) * 0.5
    half_y = (max_v[1] - min_v[1]) * 0.5
    half_z = (max_v[2] - min_v[2]) * 0.5

    return (max(half_x, 0.001), max(half_y, 0.001), max(half_z, 0.001))


def armature_obj_matrix(mesh_obj):
    """Get the armature's world matrix from a mesh child."""
    if mesh_obj.parent and mesh_obj.parent.type == 'ARMATURE':
        return mesh_obj.parent.matrix_world
    return mathutils.Matrix.Identity(4)


# ── Properties panel (Bone tab, Pose mode) ───────────────────────

class BONE_PT_talarium_physics(bpy.types.Panel):
    """Talarium per-bone physics properties."""
    bl_label = "Talarium Physics"
    bl_idname = "BONE_PT_talarium_physics"
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "bone"
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        return (context.active_pose_bone is not None or
                (context.active_bone is not None and
                 context.mode == 'POSE'))

    def draw(self, context):
        layout = self.layout
        pb = context.active_pose_bone
        if pb is None:
            return

        # ── Viewport overlay toggle ──
        row = layout.row(align=True)
        row.prop(context.scene, "talarium_show_collision",
                 text="Show Collision", icon='HIDE_OFF')
        row.operator("bone.talarium_refresh_collision",
                     text="", icon='FILE_REFRESH')

        # ── Collision shape ──
        box = layout.box()
        box.label(text="Collision Shape", icon='MESH_CUBE')
        box.prop(pb, "talarium_collision_shape")

        shape = pb.talarium_collision_shape
        if shape == '1':  # Capsule
            row = box.row(align=True)
            row.prop(pb, "talarium_capsule_radius")
            row.prop(pb, "talarium_capsule_height")
            box.prop(pb, "talarium_capsule_axis")
        elif shape == '2':  # Box
            row = box.row(align=True)
            row.prop(pb, "talarium_box_hx")
            row.prop(pb, "talarium_box_hy")
            row.prop(pb, "talarium_box_hz")
        elif shape == '3':  # Sphere
            box.prop(pb, "talarium_sphere_radius")
        elif shape == '4':  # Convex Hull
            # Show vertex group as searchable dropdown
            arm_obj = context.active_object
            mesh_child = None
            if arm_obj:
                for ch in arm_obj.children:
                    if ch.type == 'MESH':
                        mesh_child = ch
                        break
            if mesh_child:
                box.prop_search(pb, "talarium_hull_vgroup",
                                mesh_child, "vertex_groups",
                                text="Vertex Group")
            else:
                box.prop(pb, "talarium_hull_vgroup")

        if shape != '0':
            box.prop(pb, "talarium_collision_group")
            box.operator("bone.talarium_autofill_collision",
                         icon='FILE_REFRESH')

        # ── Physics flags ──
        box = layout.box()
        box.label(text="Physics", icon='PHYSICS')
        row = box.row()
        row.prop(pb, "talarium_kinematic")
        row.prop(pb, "talarium_ccd")
        box.prop(pb, "talarium_mass")

        # ── Joint type ──
        bone = pb.bone
        if bone and bone.parent:
            box = layout.box()
            box.label(text="Joint (to Parent)", icon='CONSTRAINT_BONE')
            box.prop(pb, "talarium_joint_type")

            jt = pb.talarium_joint_type
            if jt in ('2', '8'):  # Hinge or Aim
                box.prop(pb, "talarium_joint_axis")
            if jt == '2':  # Hinge
                row = box.row(align=True)
                row.prop(pb, "talarium_joint_limit_min")
                row.prop(pb, "talarium_joint_limit_max")
            elif jt == '3':  # Distance
                box.prop(pb, "talarium_joint_rest_length")
            elif jt in ('6', '7'):  # Limit rotation / position
                col = box.column(align=True)
                row = col.row(align=True)
                row.prop(pb, "talarium_joint_use_limit_x", text="X")
                row.prop(pb, "talarium_joint_limit_min_x", text="Min")
                row.prop(pb, "talarium_joint_limit_max_x", text="Max")
                row = col.row(align=True)
                row.prop(pb, "talarium_joint_use_limit_y", text="Y")
                row.prop(pb, "talarium_joint_limit_min_y", text="Min")
                row.prop(pb, "talarium_joint_limit_max_y", text="Max")
                row = col.row(align=True)
                row.prop(pb, "talarium_joint_use_limit_z", text="Z")
                row.prop(pb, "talarium_joint_limit_min_z", text="Min")
                row.prop(pb, "talarium_joint_limit_max_z", text="Max")


# ── Collision wireframe overlay ───────────────────────────────────

_draw_handler = None
_COLLIDER_WIRE_SHADER = None

# Color per collision group (group 0 = default white/cyan)
_GROUP_COLORS = [
    (0.0, 0.9, 0.9, 0.6),   # 0: cyan (default)
    (0.9, 0.3, 0.1, 0.6),   # 1: red-orange
    (0.1, 0.9, 0.2, 0.6),   # 2: green
    (0.2, 0.4, 1.0, 0.6),   # 3: blue
    (0.9, 0.9, 0.1, 0.6),   # 4: yellow
    (0.9, 0.1, 0.9, 0.6),   # 5: magenta
    (0.1, 0.9, 0.9, 0.6),   # 6: cyan-2
    (1.0, 0.6, 0.2, 0.6),   # 7: orange
]


def _circle_verts(center, axis_u, axis_v, radius, segments=16):
    """Generate circle vertex positions in 3D."""
    verts = []
    for i in range(segments):
        a = 2.0 * math.pi * i / segments
        p = center + axis_u * (radius * math.cos(a)) + axis_v * (radius * math.sin(a))
        verts.append(p)
    return verts


def _circle_lines(verts):
    """Generate line index pairs for a closed loop."""
    n = len(verts)
    return [(verts[i], verts[(i + 1) % n]) for i in range(n)]


def _capsule_wire(matrix, radius, half_height, axis_idx, segments=16):
    """Generate wireframe lines for a capsule in world space.

    axis_idx: 0=X, 1=Y, 2=Z in bone-local space.
    """
    # Bone-local axes
    axes = [matrix.col[0].to_3d().normalized(),
            matrix.col[1].to_3d().normalized(),
            matrix.col[2].to_3d().normalized()]
    center = matrix.translation.copy()

    main_axis = axes[axis_idx]
    perp1 = axes[(axis_idx + 1) % 3]
    perp2 = axes[(axis_idx + 2) % 3]

    top = center + main_axis * half_height
    bot = center - main_axis * half_height

    lines = []

    # Two circles at cylinder ends
    c_top = _circle_verts(top, perp1, perp2, radius, segments)
    c_bot = _circle_verts(bot, perp1, perp2, radius, segments)
    lines.extend(_circle_lines(c_top))
    lines.extend(_circle_lines(c_bot))

    # Four longitudinal lines
    for i in range(4):
        idx = i * (segments // 4)
        lines.append((c_top[idx], c_bot[idx]))

    # Hemisphere arcs (2 arcs per cap, in two perpendicular planes)
    for perp in [perp1, perp2]:
        for sign, cap_center in [(1.0, top), (-1.0, bot)]:
            arc = []
            half_segs = segments // 2
            for i in range(half_segs + 1):
                a = math.pi * i / half_segs
                p = (cap_center
                     + perp * (radius * math.cos(a))
                     + main_axis * (sign * radius * math.sin(a)))
                arc.append(p)
            for i in range(len(arc) - 1):
                lines.append((arc[i], arc[i + 1]))

    return lines


def _box_wire(matrix, half_extents):
    """Generate wireframe lines for a box in world space."""
    hx, hy, hz = half_extents
    center = matrix.translation.copy()
    ax = matrix.col[0].to_3d().normalized()
    ay = matrix.col[1].to_3d().normalized()
    az = matrix.col[2].to_3d().normalized()

    corners = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            for sz in (-1, 1):
                corners.append(center + ax * (sx * hx)
                               + ay * (sy * hy) + az * (sz * hz))

    # 12 edges of a box
    edges = [
        (0, 1), (2, 3), (4, 5), (6, 7),  # along Z
        (0, 2), (1, 3), (4, 6), (5, 7),  # along Y
        (0, 4), (1, 5), (2, 6), (3, 7),  # along X
    ]
    return [(corners[a], corners[b]) for a, b in edges]


def _sphere_wire(center, radius, segments=16):
    """Generate wireframe lines for a sphere (3 great circles)."""
    x = mathutils.Vector((1, 0, 0))
    y = mathutils.Vector((0, 1, 0))
    z = mathutils.Vector((0, 0, 1))
    lines = []
    lines.extend(_circle_lines(_circle_verts(center, x, y, radius, segments)))
    lines.extend(_circle_lines(_circle_verts(center, y, z, radius, segments)))
    lines.extend(_circle_lines(_circle_verts(center, x, z, radius, segments)))
    return lines


def _draw_collision_overlay():
    """Draw collision wireframes for all pose bones with shapes."""
    context = bpy.context
    if not context.scene.talarium_show_collision:
        return

    obj = context.active_object
    if not obj or obj.type != 'ARMATURE' or context.mode != 'POSE':
        return

    all_lines = []    # list of (pos_a, pos_b, color)

    for pb in obj.pose.bones:
        shape = pb.talarium_collision_shape
        if shape == '0':
            continue

        bone = pb.bone
        # Pose-space bone matrix (world-space = armature world × pose matrix)
        bone_matrix = obj.matrix_world @ pb.matrix

        group = pb.talarium_collision_group
        color = _GROUP_COLORS[group % len(_GROUP_COLORS)]

        if shape == '1':  # Capsule
            r = pb.talarium_capsule_radius
            h = pb.talarium_capsule_height
            axis_str = pb.talarium_capsule_axis
            axis_idx = {'X': 0, 'Y': 1, 'Z': 2}.get(axis_str, 1)
            lines = _capsule_wire(bone_matrix, r, h * 0.5, axis_idx)
            for a, b in lines:
                all_lines.append((a, b, color))

        elif shape == '2':  # Box
            he = (pb.talarium_box_hx, pb.talarium_box_hy, pb.talarium_box_hz)
            lines = _box_wire(bone_matrix, he)
            for a, b in lines:
                all_lines.append((a, b, color))

        elif shape == '3':  # Sphere
            r = pb.talarium_sphere_radius
            center = bone_matrix.translation.copy()
            lines = _sphere_wire(center, r)
            for a, b in lines:
                all_lines.append((a, b, color))

    if not all_lines:
        return

    # Build GPU batch
    coords = []
    colors = []
    for a, b, col in all_lines:
        coords.append(a)
        coords.append(b)
        colors.append(col)
        colors.append(col)

    shader = gpu.shader.from_builtin('FLAT_COLOR')
    batch = batch_for_shader(shader, 'LINES',
                             {"pos": coords, "color": colors})
    shader.bind()
    gpu.state.blend_set('ALPHA')
    gpu.state.line_width_set(1.5)
    gpu.state.depth_test_set('LESS_EQUAL')
    batch.draw(shader)
    gpu.state.blend_set('NONE')
    gpu.state.depth_test_set('NONE')
    gpu.state.line_width_set(1.0)


class BONE_OT_talarium_refresh_collision(bpy.types.Operator):
    """Refresh collision wireframe preview in viewport"""
    bl_idname = "bone.talarium_refresh_collision"
    bl_label = "Refresh Preview"
    bl_options = {'REGISTER'}

    def execute(self, context):
        # Force viewport redraw so the overlay updates
        for area in context.screen.areas:
            if area.type == 'VIEW_3D':
                area.tag_redraw()
        self.report({'INFO'}, "Collision preview refreshed")
        return {'FINISHED'}


# ── Registration ──────────────────────────────────────────────────

_CLASSES = [
    ExportFSKEL,
    BONE_OT_talarium_autofill_collision,
    BONE_OT_talarium_refresh_collision,
    BONE_PT_talarium_physics,
]


def menu_func_export(self, context):
    self.layout.operator(ExportFSKEL.bl_idname,
                         text="Talarium Skeleton (.fskel)")


def register():
    global _draw_handler

    # Register per-bone properties on PoseBone.
    for prop_name, prop_def in _BONE_PROPS.items():
        setattr(bpy.types.PoseBone, prop_name, prop_def)

    # Scene-level toggle for collision wireframe overlay.
    bpy.types.Scene.talarium_show_collision = BoolProperty(
        name="Show Collision",
        description="Display collision shape wireframes in the viewport",
        default=True,
    )

    for cls in _CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)

    # Install viewport draw handler for collision wireframes.
    _draw_handler = bpy.types.SpaceView3D.draw_handler_add(
        _draw_collision_overlay, (), 'WINDOW', 'POST_VIEW')


def unregister():
    global _draw_handler

    # Remove draw handler.
    if _draw_handler is not None:
        bpy.types.SpaceView3D.draw_handler_remove(_draw_handler, 'WINDOW')
        _draw_handler = None

    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    for cls in reversed(_CLASSES):
        bpy.utils.unregister_class(cls)

    for prop_name in _BONE_PROPS:
        if hasattr(bpy.types.PoseBone, prop_name):
            delattr(bpy.types.PoseBone, prop_name)

    if hasattr(bpy.types.Scene, 'talarium_show_collision'):
        del bpy.types.Scene.talarium_show_collision


if __name__ == "__main__":
    register()
