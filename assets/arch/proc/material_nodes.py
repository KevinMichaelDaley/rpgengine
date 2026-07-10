"""Procedural Romanesque material node graphs (Blender/bpy shader nodes).

Consumes the baked aperiodic material *fields* (assetsrc/materials/<mat>/fields/,
ticket rpg-gizl) and builds layered PBR materials for the arch/column/vault
assets (epic rpg-lb1q). Architecture (KMD):

  1. Field layer  -- sample a random BOX of a material's paired albedo+rough
     field, mapped to the (packed [0,1]) mesh UVs; per-object random offset for
     variation. `_field_layer` / `build_field_material`.
  2. Layer masks  -- composite several field layers via masks: geometric region,
     mesh/entity attribute, custom mask pattern, or procedural noise.
     `build_layered_material` + the `_mask_*` builders.
  3. Edge wear    -- procedural worn edges from curvature/AO. (next, rpg-u4ir)
  4. Mesoscale patterns (brick bond, coursing) come from normal + mask maps
     BAKED from real high-poly geometry (rpg-ilmc), not here.

Conventions: mesh UVs are packed into [0,1] (generators' _pack_islands), so a
layer maps the whole surface into one box of the field -- materials do not tile.
Field images live under assetsrc/materials/<mat>/fields/.
"""

import bpy
import numpy as np

FIELD_ROOT = "/home/kmd/rpg/assetsrc/materials"


# ---------------------------------------------------------------------------
# Field layer: sample a random box of a material's paired albedo+rough field
# ---------------------------------------------------------------------------

def _field_image(path, colorspace):
    """Load (or reuse) an image datablock and set its colour space."""
    img = bpy.data.images.load(path, check_existing=True)
    try:
        img.colorspace_settings.name = colorspace
    except (KeyError, TypeError):
        pass
    return img


def _image_stats(img):
    """(mean, std) of an image's red channel, read fast via foreach_get."""
    buf = np.empty(len(img.pixels), dtype=np.float32)
    img.pixels.foreach_get(buf)
    r = buf.reshape(-1, 4)[:, 0]
    return float(r.mean()), float(r.std())


def _random_box_offset(nt, x, span, salt=0.0):
    """Per-object box offset bounded to [0, span)^2 from Object Info Random.

    *salt* shifts the offset so different layers of one object sample different
    boxes. Returns a Vector output.
    """
    oi = nt.nodes.new("ShaderNodeObjectInfo")
    oi.location = (x, -340)
    mul = nt.nodes.new("ShaderNodeMath")
    mul.operation = 'MULTIPLY'
    mul.inputs[1].default_value = 7.31
    mul.location = (x + 160, -300)
    nt.links.new(oi.outputs["Random"], mul.inputs[0])
    ax = nt.nodes.new("ShaderNodeMath")
    ax.operation = 'ADD'
    ax.inputs[1].default_value = salt
    ax.location = (x + 160, -420)
    nt.links.new(oi.outputs["Random"], ax.inputs[0])
    ay = nt.nodes.new("ShaderNodeMath")
    ay.operation = 'ADD'
    ay.inputs[1].default_value = salt * 1.7
    ay.location = (x + 320, -300)
    nt.links.new(mul.outputs[0], ay.inputs[0])
    fx = nt.nodes.new("ShaderNodeMath")
    fx.operation = 'FRACT'
    fx.location = (x + 480, -420)
    nt.links.new(ax.outputs[0], fx.inputs[0])
    fy = nt.nodes.new("ShaderNodeMath")
    fy.operation = 'FRACT'
    fy.location = (x + 480, -300)
    nt.links.new(ay.outputs[0], fy.inputs[0])
    comb = nt.nodes.new("ShaderNodeCombineXYZ")
    comb.location = (x + 640, -360)
    nt.links.new(fx.outputs[0], comb.inputs["X"])
    nt.links.new(fy.outputs[0], comb.inputs["Y"])
    scale = nt.nodes.new("ShaderNodeVectorMath")
    scale.operation = 'MULTIPLY'
    scale.location = (x + 800, -360)
    scale.inputs[1].default_value = (span, span, 0.0)
    nt.links.new(comb.outputs["Vector"], scale.inputs[0])
    return scale.outputs["Vector"]


def _field_layer(nt, x, material, box_frac, rough_base, rough_detail, salt,
                 root):
    """Add nodes sampling *material*'s field as a random box; return sockets.

    Returns (colour_socket, roughness_socket). Roughness = rough_base +
    z-score(field)*rough_detail, so the base level is the material's PBR param
    and every material gets the same detail contrast regardless of its seed's.
    """
    albedo = f"{root}/{material}/fields/{material}_albedo_field.png"
    rough = f"{root}/{material}/fields/{material}_rough_field.png"
    box_frac = min(max(box_frac, 1e-3), 1.0)

    tc = nt.nodes.new("ShaderNodeTexCoord")
    tc.location = (x, 0)
    mapping = nt.nodes.new("ShaderNodeMapping")
    mapping.location = (x + 200, 0)
    mapping.inputs["Scale"].default_value = (box_frac, box_frac, 1.0)
    nt.links.new(tc.outputs["UV"], mapping.inputs["Vector"])
    nt.links.new(_random_box_offset(nt, x, 1.0 - box_frac, salt),
                 mapping.inputs["Location"])

    alb = nt.nodes.new("ShaderNodeTexImage")
    alb.location = (x + 520, 120)
    alb.image = _field_image(albedo, "sRGB")
    alb.extension = 'CLIP'
    nt.links.new(mapping.outputs["Vector"], alb.inputs["Vector"])

    rgh = nt.nodes.new("ShaderNodeTexImage")
    rgh.location = (x + 520, -160)
    rgh.image = _field_image(rough, "Non-Color")
    rgh.extension = 'CLIP'
    nt.links.new(mapping.outputs["Vector"], rgh.inputs["Vector"])

    center, sd = _image_stats(rgh.image)
    gain = rough_detail / max(sd, 1e-4)
    sub = nt.nodes.new("ShaderNodeMath")
    sub.operation = 'SUBTRACT'
    sub.location = (x + 800, -160)
    sub.inputs[1].default_value = center
    nt.links.new(rgh.outputs["Color"], sub.inputs[0])
    mul = nt.nodes.new("ShaderNodeMath")
    mul.operation = 'MULTIPLY'
    mul.location = (x + 960, -160)
    mul.inputs[1].default_value = gain
    nt.links.new(sub.outputs[0], mul.inputs[0])
    add = nt.nodes.new("ShaderNodeMath")
    add.operation = 'ADD'
    add.location = (x + 1120, -160)
    add.inputs[1].default_value = rough_base
    add.use_clamp = True
    nt.links.new(mul.outputs[0], add.inputs[0])
    return alb.outputs["Color"], add.outputs[0]


def _apply_normal(nt, bsdf, x, y, albedo_socket, normal_map, detail_strength):
    """Drive the BSDF Normal from an optional baked normal map plus a detail bump.

    *normal_map* is a tangent-space normal PNG baked to the object's packed UVs
    (from mesoscale high-poly geometry, rpg-ilmc) — sampled directly on the UVs,
    NOT through the material's random box. The detail bump refines that coarse
    baked normal with fine per-material relief derived from the (box-sampled)
    albedo luminance, chained onto the baked normal via the Bump node's Normal
    input. Either part is optional.
    """
    base = None
    if normal_map:
        tc = nt.nodes.new("ShaderNodeTexCoord")
        tc.location = (x, y)
        img = nt.nodes.new("ShaderNodeTexImage")
        img.location = (x + 200, y)
        img.image = _field_image(normal_map, "Non-Color")
        img.extension = 'CLIP'
        nt.links.new(tc.outputs["UV"], img.inputs["Vector"])
        nmap = nt.nodes.new("ShaderNodeNormalMap")
        nmap.location = (x + 520, y)
        nt.links.new(img.outputs["Color"], nmap.inputs["Color"])
        base = nmap.outputs["Normal"]
    if detail_strength > 0.0 and albedo_socket is not None:
        bw = nt.nodes.new("ShaderNodeRGBToBW")
        bw.location = (x + 520, y - 220)
        nt.links.new(albedo_socket, bw.inputs["Color"])
        bump = nt.nodes.new("ShaderNodeBump")
        bump.location = (x + 720, y - 120)
        bump.inputs["Strength"].default_value = detail_strength
        nt.links.new(bw.outputs["Val"], bump.inputs["Height"])
        if base is not None:
            nt.links.new(base, bump.inputs["Normal"])
        nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    elif base is not None:
        nt.links.new(base, bsdf.inputs["Normal"])


def build_field_material(name, material, box_frac=0.5, rough_base=0.7,
                         rough_detail=0.12, normal_map=None, detail=0.4,
                         root=FIELD_ROOT):
    """Single-material surface sampling *material*'s baked field (one layer).

    normal_map -- optional baked tangent-space normal PNG (object UVs).
    detail     -- micro detail-bump strength refining the (baked) normal.
    """
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (2000, 0)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (1700, 0)
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    color, rough = _field_layer(nt, -1200, material, box_frac, rough_base,
                                rough_detail, 0.0, root)
    nt.links.new(color, bsdf.inputs["Base Color"])
    nt.links.new(rough, bsdf.inputs["Roughness"])
    _apply_normal(nt, bsdf, 1150, -520, color, normal_map, detail)
    return mat


# ---------------------------------------------------------------------------
# Masks: scalar 0..1 signals that select between layers
# ---------------------------------------------------------------------------

def _mask_height(nt, x, y, lo, hi, invert=False):
    """Object-local Z ramped across [lo, hi] -> 0..1 (invert flips it)."""
    tc = nt.nodes.new("ShaderNodeTexCoord")
    tc.location = (x, y)
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    sep.location = (x + 200, y)
    nt.links.new(tc.outputs["Object"], sep.inputs["Vector"])
    mr = nt.nodes.new("ShaderNodeMapRange")
    mr.location = (x + 380, y)
    mr.inputs["From Min"].default_value = hi if invert else lo
    mr.inputs["From Max"].default_value = lo if invert else hi
    nt.links.new(sep.outputs["Z"], mr.inputs["Value"])
    return mr.outputs["Result"]


def _mask_attribute(nt, x, y, name):
    """A named mesh attribute / vertex colour used as a 0..1 mask (its Fac)."""
    attr = nt.nodes.new("ShaderNodeAttribute")
    attr.location = (x, y)
    attr.attribute_name = name
    return attr.outputs["Fac"]


def _mask_image(nt, x, y, path):
    """A custom mask image sampled on the packed UVs (red channel, 0..1)."""
    tc = nt.nodes.new("ShaderNodeTexCoord")
    tc.location = (x, y)
    img = nt.nodes.new("ShaderNodeTexImage")
    img.location = (x + 200, y)
    img.image = _field_image(path, "Non-Color")
    img.extension = 'CLIP'
    nt.links.new(tc.outputs["UV"], img.inputs["Vector"])
    return img.outputs["Color"]


def _mask_noise(nt, x, y, scale=6.0, lo=0.4, hi=0.6):
    """Procedural noise thresholded into a soft 0..1 mask for organic patches."""
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.location = (x, y)
    noise.inputs["Scale"].default_value = scale
    mr = nt.nodes.new("ShaderNodeMapRange")
    mr.location = (x + 200, y)
    mr.inputs["From Min"].default_value = lo
    mr.inputs["From Max"].default_value = hi
    nt.links.new(noise.outputs["Fac"], mr.inputs["Value"])
    return mr.outputs["Result"]


def _sharpen(nt, x, y, socket, hardness):
    """Steepen a 0..1 mask about 0.5 so its transition can be hard or soft.

    hardness 0 leaves it linear (soft gradient); ->1 collapses the transition to
    a hard step/line at the mask's 0.5 crossing (e.g. a masonry string course
    between a plinth and the dressing above). A tiny residual width keeps the
    edge anti-aliased rather than pixel-jagged.
    """
    hardness = min(max(hardness, 0.0), 1.0)
    if hardness <= 0.0:
        return socket
    w = max((1.0 - hardness) * 0.5, 0.004)
    mr = nt.nodes.new("ShaderNodeMapRange")
    mr.location = (x, y)
    mr.inputs["From Min"].default_value = 0.5 - w
    mr.inputs["From Max"].default_value = 0.5 + w
    nt.links.new(socket, mr.inputs["Value"])
    return mr.outputs["Result"]


def _build_mask(nt, spec, x, y):
    """Dispatch a mask spec dict to its builder, then apply its `hardness`
    (0 = soft gradient .. 1 = hard step). Returns a scalar 0..1 socket."""
    kind = spec["type"]
    if kind == "height":
        socket = _mask_height(nt, x, y, spec["min"], spec["max"],
                             spec.get("invert", False))
    elif kind == "attribute":
        socket = _mask_attribute(nt, x, y, spec["name"])
    elif kind == "image":
        socket = _mask_image(nt, x, y, spec["path"])
    elif kind == "noise":
        socket = _mask_noise(nt, x, y, spec.get("scale", 6.0),
                            spec.get("lo", 0.4), spec.get("hi", 0.6))
    else:
        raise ValueError(f"unknown mask type: {kind}")
    return _sharpen(nt, x + 620, y, socket, spec.get("hardness", 0.0))


# ---------------------------------------------------------------------------
# Layered material: stack of field layers composited by masks
# ---------------------------------------------------------------------------

def _mix_color(nt, x, y, a_socket, b_socket, fac_socket):
    """MixRGB: fac 0 -> a, 1 -> b. Returns the Color output."""
    mix = nt.nodes.new("ShaderNodeMixRGB")
    mix.location = (x, y)
    nt.links.new(fac_socket, mix.inputs["Fac"])
    nt.links.new(a_socket, mix.inputs["Color1"])
    nt.links.new(b_socket, mix.inputs["Color2"])
    return mix.outputs["Color"]


def _mix_float(nt, x, y, a_socket, b_socket, fac_socket):
    """Linear float mix a + (b-a)*fac, via Math nodes (avoids Mix-node socket
    ambiguity). Returns the result socket."""
    sub = nt.nodes.new("ShaderNodeMath")
    sub.operation = 'SUBTRACT'
    sub.location = (x, y)
    nt.links.new(b_socket, sub.inputs[0])
    nt.links.new(a_socket, sub.inputs[1])
    mul = nt.nodes.new("ShaderNodeMath")
    mul.operation = 'MULTIPLY'
    mul.location = (x + 160, y)
    nt.links.new(sub.outputs[0], mul.inputs[0])
    nt.links.new(fac_socket, mul.inputs[1])
    add = nt.nodes.new("ShaderNodeMath")
    add.operation = 'ADD'
    add.location = (x + 320, y)
    nt.links.new(mul.outputs[0], add.inputs[0])
    nt.links.new(a_socket, add.inputs[1])
    return add.outputs[0]


def build_layered_material(name, layers, normal_map=None, detail=0.4,
                           root=FIELD_ROOT):
    """Composite a stack of field layers via per-layer masks.

    normal_map -- optional baked tangent-space normal PNG on the object UVs
                  (from mesoscale geometry, rpg-ilmc), refined by a detail bump.
    detail     -- micro detail-bump strength.

    layers: list of dicts, painted bottom to top. Each layer:
        material     -- material key.
        box_frac     -- field box fraction (default 0.5).
        rough_base   -- base roughness (default 0.7).
        rough_detail -- roughness detail amplitude (default 0.12).
        mask         -- (all but the first) a mask spec dict selecting where this
                        layer shows over the ones below (1 = this layer):
                          {"type":"height", "min":z0, "max":z1, "invert":False}
                          {"type":"attribute", "name":"<attr>"}
                          {"type":"image", "path":"<png>"}
                          {"type":"noise", "scale":.., "lo":.., "hi":..}
                        Any mask also takes "hardness" (0 soft gradient .. 1 hard
                        step/line, e.g. a masonry string course).
    Returns the material datablock.
    """
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (2400, 0)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (2100, 0)
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

    base = layers[0]
    color, rough = _field_layer(nt, -1800, base["material"],
                                base.get("box_frac", 0.5),
                                base.get("rough_base", 0.7),
                                base.get("rough_detail", 0.12), 0.0, root)
    for i, layer in enumerate(layers[1:], start=1):
        c_i, r_i = _field_layer(nt, -1800, layer["material"],
                                layer.get("box_frac", 0.5),
                                layer.get("rough_base", 0.7),
                                layer.get("rough_detail", 0.12),
                                float(i) * 0.37, root)
        fac = _build_mask(nt, layer["mask"], 1500, -500 - i * 260)
        color = _mix_color(nt, 1800, 200 - i * 40, color, c_i, fac)
        rough = _mix_float(nt, 1800, -200 - i * 40, rough, r_i, fac)
    nt.links.new(color, bsdf.inputs["Base Color"])
    nt.links.new(rough, bsdf.inputs["Roughness"])
    _apply_normal(nt, bsdf, 1500, -1100, color, normal_map, detail)
    return mat
