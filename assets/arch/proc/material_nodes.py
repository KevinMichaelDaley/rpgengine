"""Procedural Romanesque material node graphs (Blender/bpy shader nodes).

Consumes the baked aperiodic material *fields* (assetsrc/materials/<mat>/fields/,
ticket rpg-gizl) and builds layered PBR materials for the arch/column/vault
assets (epic rpg-lb1q). Architecture (KMD):

  1. Field layer  -- sample a random BOX of a material's paired albedo+rough
     field, mapped to the mesh UVs (per-object random offset for variation).
  2. Layer masks  -- composite several field layers via masks: geometric region,
     mesh/entity attribute, or a custom mask pattern. (next)
  3. Edge wear    -- procedural worn edges from curvature/AO/pointiness. (next)
  4. Mesoscale patterns (brick bond, stonework coursing) come from normal +
     mask maps BAKED from real high-poly geometry in a separate graph, not here.

This module currently implements step 1 (the field layer / single-material
surface). Steps 2-4 build on it.

Conventions: UVs are unwrapped at UV_SCALE = 1.0 (1 UV unit = 1 metre), so the
field's real-world size is one parameter (`texel_scale`, metres spanned by the
whole field). Field images live under assetsrc/materials/<mat>/fields/.
"""

import bpy
import numpy as np

FIELD_ROOT = "/home/kmd/rpg/assetsrc/materials"


def _image_stats(img):
    """(mean, std) of an image's red channel, read fast via foreach_get."""
    buf = np.empty(len(img.pixels), dtype=np.float32)
    img.pixels.foreach_get(buf)
    r = buf.reshape(-1, 4)[:, 0]
    return float(r.mean()), float(r.std())


def _field_image(path, colorspace):
    """Load (or reuse) an image datablock and set its colour space."""
    img = bpy.data.images.load(path, check_existing=True)
    try:
        img.colorspace_settings.name = colorspace
    except (KeyError, TypeError):
        pass
    return img


def _random_box_offset(nt, x, span):
    """Nodes producing a per-object box offset from Object Info, bounded to
    [0, span)^2 so the sampled box stays inside the field's [0,1] bounds.

    Each object samples a different box of the aperiodic field, so instances of
    the same material do not look identical. Returns a Vector output.
    """
    oi = nt.nodes.new("ShaderNodeObjectInfo")
    oi.location = (x, -320)
    mul = nt.nodes.new("ShaderNodeMath")
    mul.operation = 'MULTIPLY'
    mul.inputs[1].default_value = 7.31
    mul.location = (x + 180, -400)
    frac = nt.nodes.new("ShaderNodeMath")
    frac.operation = 'FRACT'
    frac.location = (x + 360, -400)
    nt.links.new(oi.outputs["Random"], mul.inputs[0])
    nt.links.new(mul.outputs[0], frac.inputs[0])
    comb = nt.nodes.new("ShaderNodeCombineXYZ")
    comb.location = (x + 540, -360)
    nt.links.new(oi.outputs["Random"], comb.inputs["X"])
    nt.links.new(frac.outputs[0], comb.inputs["Y"])
    scale = nt.nodes.new("ShaderNodeVectorMath")
    scale.operation = 'MULTIPLY'
    scale.location = (x + 720, -360)
    scale.inputs[1].default_value = (span, span, 0.0)
    nt.links.new(comb.outputs["Vector"], scale.inputs[0])
    return scale.outputs["Vector"]


def build_field_material(name, material, box_frac=0.5, rough_base=0.7,
                         rough_detail=0.12, root=FIELD_ROOT):
    """Build a single-material surface that samples *material*'s baked field.

    The mesh UVs are packed into [0,1] (see generators' _pack_islands), so the
    material maps the whole surface into a random BOX of the aperiodic field:
    box side = *box_frac* of the field, at a per-object random offset (bounded so
    the box stays inside [0,1] -> no tiling/repeat). Smaller box_frac zooms in
    (bigger features); different objects sample different boxes.

    Args:
        name         -- new material datablock name.
        material     -- material key (e.g. "limestone"); reads its albedo/rough
                        field PNGs from <root>/<material>/fields/.
        box_frac     -- fraction of the field each object samples (0<box_frac<=1).
        rough_base   -- base roughness (the material's PBR parameter); the
                        roughness field only modulates detail around it.
        rough_detail -- roughness detail amplitude. The field is z-score
                        normalised (zero-mean/unit-std) so every material gets
                        this same detail contrast regardless of its seed's
                        (low, varying) native contrast, then scaled by this and
                        added to rough_base.
    Returns the material datablock.
    """
    albedo = f"{root}/{material}/fields/{material}_albedo_field.png"
    rough = f"{root}/{material}/fields/{material}_rough_field.png"

    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()

    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (720, 0)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (420, 0)
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

    tc = nt.nodes.new("ShaderNodeTexCoord")
    tc.location = (-1000, 0)
    mapping = nt.nodes.new("ShaderNodeMapping")
    mapping.location = (-400, 0)
    box_frac = min(max(box_frac, 1e-3), 1.0)
    mapping.inputs["Scale"].default_value = (box_frac, box_frac, 1.0)
    nt.links.new(tc.outputs["UV"], mapping.inputs["Vector"])
    nt.links.new(_random_box_offset(nt, -1000, 1.0 - box_frac),
                 mapping.inputs["Location"])

    alb = nt.nodes.new("ShaderNodeTexImage")
    alb.location = (-100, 200)
    alb.image = _field_image(albedo, "sRGB")
    alb.extension = 'CLIP'
    nt.links.new(mapping.outputs["Vector"], alb.inputs["Vector"])
    nt.links.new(alb.outputs["Color"], bsdf.inputs["Base Color"])

    rgh = nt.nodes.new("ShaderNodeTexImage")
    rgh.location = (-100, -160)
    rgh.image = _field_image(rough, "Non-Color")
    rgh.extension = 'CLIP'
    nt.links.new(mapping.outputs["Vector"], rgh.inputs["Vector"])

    # Roughness = rough_base + z-score(field) * rough_detail, clamped.
    # z-score standardises the field's (narrow, per-material) contrast so the
    # base level comes from the parameter, not the field's arbitrary mean.
    center, sd = _image_stats(rgh.image)
    gain = rough_detail / max(sd, 1e-4)
    sub = nt.nodes.new("ShaderNodeMath")
    sub.operation = 'SUBTRACT'
    sub.location = (150, -160)
    sub.inputs[1].default_value = center
    nt.links.new(rgh.outputs["Color"], sub.inputs[0])
    mul = nt.nodes.new("ShaderNodeMath")
    mul.operation = 'MULTIPLY'
    mul.location = (330, -160)
    mul.inputs[1].default_value = gain
    nt.links.new(sub.outputs[0], mul.inputs[0])
    add = nt.nodes.new("ShaderNodeMath")
    add.operation = 'ADD'
    add.location = (510, -160)
    add.inputs[1].default_value = rough_base
    add.use_clamp = True
    nt.links.new(mul.outputs[0], add.inputs[0])
    nt.links.new(add.outputs[0], bsdf.inputs["Roughness"])
    return mat
