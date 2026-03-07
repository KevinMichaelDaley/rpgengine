"""
Blender Add-on: Export .fvma (Talarium Mesh Format)

Exports the active mesh object's geometry, UVs, normals, tangents,
vertex colors, and skeletal bone weights/indices to the binary .fvma
format consumed by the Talarium engine.

When an Armature modifier is present and an .fskel file has been
exported for the same armature, bone indices in the FVMA match the
bone ordering from the armature (same order as .fskel).

Install:  Edit → Preferences → Add-ons → Install from Disk → select this file.
Usage:    File → Export → Talarium Mesh (.fvma)

Compatible with Blender 3.6+ / 4.x.
"""

bl_info = {
    "name": "Talarium Mesh (.fvma)",
    "author": "Talarium Engine Team",
    "version": (1, 0, 0),
    "blender": (3, 6, 0),
    "location": "File > Export > Talarium Mesh (.fvma)",
    "description": "Export mesh geometry with skeletal weights to .fvma",
    "category": "Import-Export",
}

import bpy
import struct
import mathutils
from bpy_extras.io_utils import ExportHelper
from bpy.props import StringProperty, BoolProperty

# ── Format constants (must match mesh_vao_format.h) ─────────────────

FVMA_MAGIC   = 0x414D5646   # 'FVMA' little-endian
FVMA_VERSION = 1
FVMA_HEADER_SIZE = 24

MESH_VAO_FLAG_NORMALS  = (1 << 0)
MESH_VAO_FLAG_TANGENTS = (1 << 1)
MESH_VAO_FLAG_UV0      = (1 << 2)
MESH_VAO_FLAG_UV1      = (1 << 3)
MESH_VAO_FLAG_COLORS   = (1 << 4)
MESH_VAO_FLAG_BONES    = (1 << 5)

# Max bone influences per vertex (engine uses vec4 for weights/indices).
MAX_BONE_INFLUENCES = 4


# ── Coordinate conversion ───────────────────────────────────────────

def _convert_pos(co):
    """Blender Z-up → engine Y-up: (x, y, z) → (x, z, -y)."""
    return (co[0], co[2], -co[1])


def _convert_normal(n):
    """Same as position for direction vectors."""
    return (n[0], n[2], -n[1])


def _convert_tangent(t, sign):
    """Convert tangent vec3 + bitangent sign."""
    return (t[0], t[2], -t[1], sign)


# ── Mesh triangulation and data gathering ───────────────────────────

def _gather_mesh_data(obj, export_normals, export_tangents, export_uvs,
                      export_colors, export_bones):
    """
    Triangulate and gather all vertex attribute data from a Blender mesh.

    Single-pass approach: we track the original vertex index alongside
    each split vertex so bone weights can be looked up without a second
    mesh evaluation.

    Returns a dict with:
        positions:    flat list of floats (vec3 per vertex)
        normals:      flat list of floats (vec3 per vertex) or None
        tangents:     flat list of floats (vec4 per vertex) or None
        uv0:          flat list of floats (vec2 per vertex) or None
        uv1:          flat list of floats (vec2 per vertex) or None
        colors:       flat list of floats (vec4 per vertex) or None
        indices:      flat list of uint32
        polygroups:   flat list of uint16 (one per triangle)
        bone_weights: flat list of floats (vec4 per vertex) or None
        bone_indices: flat list of uint32 (uvec4 per vertex) or None
        bone_count:   uint32 or 0
        ibms:         flat list of floats (mat4 per bone, col-major) or None
        flags:        uint32 FVMA flag bitmask
    """
    # Force armature to rest pose before evaluating, so mesh vertices
    # are exported in bind pose (matching the inverse-bind matrices).
    armature_obj_tmp = None
    old_pose_position = None
    for mod in obj.modifiers:
        if mod.type == 'ARMATURE' and mod.object:
            armature_obj_tmp = mod.object
            old_pose_position = armature_obj_tmp.data.pose_position
            armature_obj_tmp.data.pose_position = 'REST'
            break

    # Get the evaluated (modifier-applied) mesh.
    depsgraph = bpy.context.evaluated_depsgraph_get()
    eval_obj = obj.evaluated_get(depsgraph)
    mesh = eval_obj.to_mesh()

    # Restore armature pose position.
    if armature_obj_tmp and old_pose_position:
        armature_obj_tmp.data.pose_position = old_pose_position

    # Compute smooth normals and tangents.
    # calc_normals_split() was removed in Blender 4.0;
    # corner_normals are now auto-computed.
    if export_normals:
        if hasattr(mesh, 'calc_normals_split'):
            mesh.calc_normals_split()
    if export_tangents and mesh.uv_layers:
        mesh.calc_tangents()

    # Build bone name → index mapping from the armature.
    bone_name_to_idx = {}
    armature_obj = None
    if export_bones:
        for mod in obj.modifiers:
            if mod.type == 'ARMATURE' and mod.object:
                armature_obj = mod.object
                break
        if armature_obj:
            armature = armature_obj.data
            for idx, bone in enumerate(armature.bones):
                bone_name_to_idx[bone.name] = idx

    has_bones = len(bone_name_to_idx) > 0

    # Pre-compute per-original-vertex bone data.
    vert_bone_weights = {}
    vert_bone_indices = {}
    if has_bones:
        for vi, vert in enumerate(mesh.vertices):
            groups = []
            for g in vert.groups:
                vg = obj.vertex_groups[g.group]
                bone_idx = bone_name_to_idx.get(vg.name, None)
                if bone_idx is not None and g.weight > 0.0:
                    groups.append((bone_idx, g.weight))
            # Sort by weight descending, keep top MAX_BONE_INFLUENCES.
            groups.sort(key=lambda x: x[1], reverse=True)
            groups = groups[:MAX_BONE_INFLUENCES]
            # Pad to MAX_BONE_INFLUENCES.
            while len(groups) < MAX_BONE_INFLUENCES:
                groups.append((0, 0.0))
            vert_bone_indices[vi] = [g[0] for g in groups]
            vert_bone_weights[vi] = [g[1] for g in groups]

    # UV layers.
    uv0_layer = mesh.uv_layers[0] if (export_uvs and mesh.uv_layers) else None
    uv1_layer = (mesh.uv_layers[1]
                 if (export_uvs and len(mesh.uv_layers) > 1) else None)

    # Vertex color layer.
    color_layer = None
    if export_colors and mesh.color_attributes:
        color_layer = mesh.color_attributes[0]

    # Output arrays.
    positions = []
    normals = []
    tangents = []
    uv0_data = []
    uv1_data = []
    color_data = []
    indices = []
    polygroups = []

    # Bone data per split vertex (indexed by new vertex index).
    split_bone_weights = []  # list of [w0,w1,w2,w3] per split vertex
    split_bone_indices = []  # list of [b0,b1,b2,b3] per split vertex

    # Vertex deduplication: key → (new_vertex_index, original_vertex_index).
    vertex_map = {}
    vertex_count = 0

    for poly in mesh.polygons:
        # Triangulate polygons using fan from first vertex.
        loop_list = list(poly.loop_indices)
        tri_fans = []
        if len(loop_list) == 3:
            tri_fans.append(loop_list)
        else:
            for i in range(1, len(loop_list) - 1):
                tri_fans.append([loop_list[0], loop_list[i],
                                 loop_list[i + 1]])

        for tri in tri_fans:
            tri_indices = []
            for loop_idx in tri:
                loop = mesh.loops[loop_idx]
                vi = loop.vertex_index
                co = mesh.vertices[vi].co

                # Build dedup key from all per-loop attributes.
                pos = _convert_pos(co)
                key_parts = [pos]

                norm = None
                if export_normals:
                    # Blender 4.0+ uses corner_normals; older uses loop.normal
                    if hasattr(mesh, 'corner_normals') and mesh.corner_normals:
                        norm = _convert_normal(mesh.corner_normals[loop_idx].vector)
                    else:
                        norm = _convert_normal(loop.normal)
                    key_parts.append(norm)

                tang = None
                if export_tangents and mesh.uv_layers:
                    tang = _convert_tangent(loop.tangent, loop.bitangent_sign)
                    key_parts.append(tang)

                uv0 = None
                if uv0_layer:
                    uv = uv0_layer.data[loop_idx].uv
                    uv0 = (uv[0], 1.0 - uv[1])  # Flip V for OpenGL
                    key_parts.append(uv0)

                uv1 = None
                if uv1_layer:
                    uv = uv1_layer.data[loop_idx].uv
                    uv1 = (uv[0], 1.0 - uv[1])
                    key_parts.append(uv1)

                col = None
                if color_layer:
                    if color_layer.domain == 'CORNER':
                        c = color_layer.data[loop_idx].color
                    else:
                        c = color_layer.data[vi].color
                    col = (c[0], c[1], c[2], c[3] if len(c) > 3 else 1.0)
                    key_parts.append(col)

                # Flatten all components into a hashable key.
                key = tuple(round(v, 6) for part in key_parts
                            for v in (part if isinstance(part, tuple)
                                      else (part,)))

                if key in vertex_map:
                    tri_indices.append(vertex_map[key])
                else:
                    new_idx = vertex_count
                    vertex_map[key] = new_idx
                    vertex_count += 1

                    positions.extend(pos)
                    if norm:
                        normals.extend(norm)
                    if tang:
                        tangents.extend(tang)
                    if uv0:
                        uv0_data.extend(uv0)
                    if uv1:
                        uv1_data.extend(uv1)
                    if col:
                        color_data.extend(col)

                    # Track bone data for this split vertex.
                    if has_bones:
                        w = vert_bone_weights.get(vi, [0, 0, 0, 0])
                        b = vert_bone_indices.get(vi, [0, 0, 0, 0])
                        split_bone_weights.append(w)
                        split_bone_indices.append(b)

                    tri_indices.append(new_idx)

            indices.extend(tri_indices)
            polygroups.append(poly.material_index)

    # Flatten bone data arrays.
    bone_weights_flat = None
    bone_indices_flat = None
    bone_count = 0
    ibms = None
    if has_bones and split_bone_weights:
        bone_count = len(bone_name_to_idx)
        bone_weights_flat = []
        bone_indices_flat = []
        for w in split_bone_weights:
            bone_weights_flat.extend(w)
        for b in split_bone_indices:
            bone_indices_flat.extend(b)

        # Compute inverse bind matrices (column-major, matching glTF path).
        ibms = []
        for bone in armature_obj.data.bones:
            bone_world = armature_obj.matrix_world @ bone.matrix_local
            ibm_bl = bone_world.inverted_safe()
            ibm_engine = _blender_to_engine_matrix(ibm_bl)
            ibms.extend(ibm_engine)

    eval_obj.to_mesh_clear()

    # Build flags.
    flags = 0
    if export_normals and normals:
        flags |= MESH_VAO_FLAG_NORMALS
    if export_tangents and tangents:
        flags |= MESH_VAO_FLAG_TANGENTS
    if uv0_data:
        flags |= MESH_VAO_FLAG_UV0
    if uv1_data:
        flags |= MESH_VAO_FLAG_UV1
    if color_data:
        flags |= MESH_VAO_FLAG_COLORS
    if has_bones and bone_weights_flat:
        flags |= MESH_VAO_FLAG_BONES

    return {
        'positions': positions,
        'normals': normals if (flags & MESH_VAO_FLAG_NORMALS) else None,
        'tangents': tangents if (flags & MESH_VAO_FLAG_TANGENTS) else None,
        'uv0': uv0_data if (flags & MESH_VAO_FLAG_UV0) else None,
        'uv1': uv1_data if (flags & MESH_VAO_FLAG_UV1) else None,
        'colors': color_data if (flags & MESH_VAO_FLAG_COLORS) else None,
        'indices': indices,
        'polygroups': polygroups,
        'bone_weights': bone_weights_flat,
        'bone_indices': bone_indices_flat,
        'bone_count': bone_count,
        'ibms': ibms,
        'flags': flags,
        'vertex_count': vertex_count,
        'index_count': len(indices),
    }


# ── Matrix conversion (same as export_fskel.py) ────────────────────

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


def _blender_to_engine_matrix(bmat):
    """Convert Blender 4×4 matrix to engine column-major float[16]."""
    bm = [[bmat[r][c] for c in range(4)] for r in range(4)]
    em = _mat4_mul(_COORD_C, _mat4_mul(bm, _COORD_C_INV))
    m = [0.0] * 16
    for col in range(4):
        for row in range(4):
            m[col * 4 + row] = em[row][col]
    return m


# ── Binary writer ──────────────────────────────────────────────────

def _write_fvma(filepath, data):
    """Write mesh data dict to FVMA binary file."""
    vc = data['vertex_count']
    ic = data['index_count']
    fc = ic // 3
    flags = data['flags']

    with open(filepath, 'wb') as f:
        # Header (24 bytes).
        f.write(struct.pack('<IIIIII',
                            FVMA_MAGIC, FVMA_VERSION,
                            vc, ic, flags, 0))

        # Positions (always present).
        f.write(struct.pack(f'<{vc * 3}f', *data['positions']))

        # Optional attributes.
        if flags & MESH_VAO_FLAG_NORMALS:
            f.write(struct.pack(f'<{vc * 3}f', *data['normals']))

        if flags & MESH_VAO_FLAG_TANGENTS:
            f.write(struct.pack(f'<{vc * 4}f', *data['tangents']))

        if flags & MESH_VAO_FLAG_UV0:
            f.write(struct.pack(f'<{vc * 2}f', *data['uv0']))

        if flags & MESH_VAO_FLAG_UV1:
            f.write(struct.pack(f'<{vc * 2}f', *data['uv1']))

        if flags & MESH_VAO_FLAG_COLORS:
            f.write(struct.pack(f'<{vc * 4}f', *data['colors']))

        # Indices.
        f.write(struct.pack(f'<{ic}I', *data['indices']))

        # Polygroup IDs (uint16 per face).
        f.write(struct.pack(f'<{fc}H', *data['polygroups']))

        # Bone data footer (if BONES flag set).
        if flags & MESH_VAO_FLAG_BONES:
            bone_count = data['bone_count']
            f.write(struct.pack('<I', bone_count))

            # Bone weights (vec4 per vertex, float).
            f.write(struct.pack(f'<{vc * 4}f', *data['bone_weights']))

            # Bone indices (uvec4 per vertex, uint32).
            f.write(struct.pack(f'<{vc * 4}I', *data['bone_indices']))

            # Inverse bind matrices (mat4 per bone, column-major float).
            f.write(struct.pack(f'<{bone_count * 16}f', *data['ibms']))


# ── Main export logic ──────────────────────────────────────────────

def export_fvma(context, filepath, export_normals, export_tangents,
                export_uvs, export_colors, export_bones):
    """
    Export the active mesh object to .fvma format.

    Returns set of {'FINISHED'} or {'CANCELLED'}.
    """
    obj = context.active_object
    if not obj or obj.type != 'MESH':
        for o in context.scene.objects:
            if o.type == 'MESH':
                obj = o
                break
    if not obj or obj.type != 'MESH':
        raise RuntimeError("No mesh object found in scene")

    data = _gather_mesh_data(obj, export_normals, export_tangents,
                             export_uvs, export_colors, export_bones)

    _write_fvma(filepath, data)

    vc = data['vertex_count']
    ic = data['index_count']
    bc = data['bone_count']
    flags = data['flags']

    flag_names = []
    if flags & MESH_VAO_FLAG_NORMALS:  flag_names.append('normals')
    if flags & MESH_VAO_FLAG_TANGENTS: flag_names.append('tangents')
    if flags & MESH_VAO_FLAG_UV0:      flag_names.append('uv0')
    if flags & MESH_VAO_FLAG_UV1:      flag_names.append('uv1')
    if flags & MESH_VAO_FLAG_COLORS:   flag_names.append('colors')
    if flags & MESH_VAO_FLAG_BONES:    flag_names.append(f'bones({bc})')

    print(f"Exported {filepath}:")
    print(f"  {vc} vertices, {ic // 3} triangles, flags: {', '.join(flag_names)}")

    return {'FINISHED'}


# ── Blender operator ──────────────────────────────────────────────

class ExportFVMA(bpy.types.Operator, ExportHelper):
    """Export active mesh to Talarium .fvma format"""
    bl_idname = "export_mesh.fvma"
    bl_label = "Export .fvma"
    bl_options = {'PRESET'}

    filename_ext = ".fvma"

    filter_glob: StringProperty(
        default="*.fvma",
        options={'HIDDEN'},
        maxlen=255,
    )

    export_normals: BoolProperty(
        name="Export Normals",
        description="Include vertex normals",
        default=True,
    )

    export_tangents: BoolProperty(
        name="Export Tangents",
        description="Include tangent vectors (requires UVs)",
        default=True,
    )

    export_uvs: BoolProperty(
        name="Export UVs",
        description="Include texture coordinates",
        default=True,
    )

    export_colors: BoolProperty(
        name="Export Vertex Colors",
        description="Include vertex color attributes",
        default=False,
    )

    export_bones: BoolProperty(
        name="Export Bone Weights",
        description="Include skeletal bone weights and indices from "
                    "Armature modifier (for skinned meshes)",
        default=True,
    )

    def execute(self, context):
        try:
            result = export_fvma(context, self.filepath,
                                 self.export_normals, self.export_tangents,
                                 self.export_uvs, self.export_colors,
                                 self.export_bones)
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
        layout.prop(self, "export_normals")
        layout.prop(self, "export_tangents")
        layout.prop(self, "export_uvs")
        layout.prop(self, "export_colors")
        layout.prop(self, "export_bones")


# ── Registration ──────────────────────────────────────────────────

def menu_func_export(self, context):
    self.layout.operator(ExportFVMA.bl_idname,
                         text="Talarium Mesh (.fvma)")


def register():
    bpy.utils.register_class(ExportFVMA)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    bpy.utils.unregister_class(ExportFVMA)


if __name__ == "__main__":
    register()
