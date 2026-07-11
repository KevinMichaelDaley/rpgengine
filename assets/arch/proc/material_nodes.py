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

import os

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
                 root, tint=(1.0, 1.0, 1.0)):
    """Add nodes sampling *material*'s field as a random box; return sockets.

    Returns (colour_socket, roughness_socket). Roughness = rough_base +
    z-score(field)*rough_detail, so the base level is the material's PBR param
    and every material gets the same detail contrast regardless of its seed's.
    *tint* multiplies the albedo (default white = unchanged) to shade a layer
    darker/lighter — e.g. to separate masked layers tonally, or hit a PBR hue.
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

    color_out = alb.outputs["Color"]
    if tuple(tint) != (1.0, 1.0, 1.0):
        tintn = nt.nodes.new("ShaderNodeMixRGB")
        tintn.blend_type = 'MULTIPLY'
        tintn.location = (x + 800, 120)
        tintn.inputs["Fac"].default_value = 1.0
        tintn.inputs["Color2"].default_value = (tint[0], tint[1], tint[2], 1.0)
        nt.links.new(alb.outputs["Color"], tintn.inputs["Color1"])
        color_out = tintn.outputs["Color"]
    return color_out, add.outputs[0]


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
                         rough_detail=0.12, tint=(1.0, 1.0, 1.0),
                         normal_map=None, detail=0.4, root=FIELD_ROOT):
    """Single-material surface sampling *material*'s baked field (one layer).

    tint       -- albedo multiplier (default white) to shade darker/lighter.
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
                                rough_detail, 0.0, root, tint)
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
                                base.get("rough_detail", 0.12), 0.0, root,
                                base.get("tint", (1.0, 1.0, 1.0)))
    for i, layer in enumerate(layers[1:], start=1):
        c_i, r_i = _field_layer(nt, -1800, layer["material"],
                                layer.get("box_frac", 0.5),
                                layer.get("rough_base", 0.7),
                                layer.get("rough_detail", 0.12),
                                float(i) * 0.37, root,
                                layer.get("tint", (1.0, 1.0, 1.0)))
        fac = _build_mask(nt, layer["mask"], 1500, -500 - i * 260)
        color = _mix_color(nt, 1800, 200 - i * 40, color, c_i, fac)
        rough = _mix_float(nt, 1800, -200 - i * 40, rough, r_i, fac)
    nt.links.new(color, bsdf.inputs["Base Color"])
    nt.links.new(rough, bsdf.inputs["Roughness"])
    _apply_normal(nt, bsdf, 1500, -1100, color, normal_map, detail)
    return mat


# ---------------------------------------------------------------------------
# Masonry material: tiling brick wall from baked maps + limestone/mortar fields
# ---------------------------------------------------------------------------
# Unlike the field-box materials above (one [0,1] box per small object), a wall
# is a TILING surface: the mesoscale maps baked from the high-poly wall
# (rpg-ilmc: mask / height / normal / ao, a seamless [0,W]x[0,H] tile) and the
# limestone/mortar fields are all sampled on WORLD-SCALED UVs with REPEAT, so the
# stones come out at real size on any object (here an arched windowsill).

def _mapping(nt, x, y, uv_out, span_x, span_y, rot_deg=0.0):
    """UV -> a Mapping scaled so one repeat spans (span_x, span_y) metres, with an
    optional Z rotation (to align brick coursing horizontal to the UV island)."""
    m = nt.nodes.new("ShaderNodeMapping")
    m.location = (x, y)
    m.inputs["Scale"].default_value = (1.0 / span_x, 1.0 / span_y, 1.0)
    m.inputs["Rotation"].default_value = (0.0, 0.0, np.radians(rot_deg))
    nt.links.new(uv_out, m.inputs["Vector"])
    return m.outputs["Vector"]


def _tex(nt, x, y, path, colorspace, mapping_out, extension='REPEAT'):
    """An image texture sampled through ``mapping_out``. Returns the node."""
    img = nt.nodes.new("ShaderNodeTexImage")
    img.location = (x, y)
    img.image = _field_image(path, colorspace)
    img.extension = extension
    nt.links.new(mapping_out, img.inputs["Vector"])
    return img


def _tint(nt, x, y, color_socket, tint):
    mul = nt.nodes.new("ShaderNodeMixRGB")
    mul.location = (x, y)
    mul.blend_type = 'MULTIPLY'
    mul.inputs["Fac"].default_value = 1.0
    mul.inputs["Color2"].default_value = (*tint, 1.0)
    nt.links.new(color_socket, mul.inputs["Color1"])
    return mul.outputs["Color"]


def _contrast(nt, x, y, color_socket, contrast, bright=0.0):
    """Bright/Contrast node (skipped when both are zero)."""
    if contrast == 0.0 and bright == 0.0:
        return color_socket
    bc = nt.nodes.new("ShaderNodeBrightContrast")
    bc.location = (x, y)
    bc.inputs["Bright"].default_value = bright
    bc.inputs["Contrast"].default_value = contrast
    nt.links.new(color_socket, bc.inputs["Color"])
    return bc.outputs["Color"]


def _bw_scale(nt, x, y, color_socket, scale):
    """Grayscale a (roughness) image and scale it -> scalar roughness socket."""
    bw = nt.nodes.new("ShaderNodeRGBToBW")
    bw.location = (x, y)
    nt.links.new(color_socket, bw.inputs["Color"])
    m = nt.nodes.new("ShaderNodeMath")
    m.operation = 'MULTIPLY'
    m.location = (x + 160, y)
    m.inputs[1].default_value = scale
    nt.links.new(bw.outputs["Val"], m.inputs[0])
    return m.outputs["Value"]


def _edge_wear(nt, x, y, albedo, ao_scalar, brick_fac, wear_color, strength,
               ao_lo, ao_hi, band, rough=None, rough_delta=0.0):
    """Edge-wear layer derived from the baked AO map (nodes only).

    The wall's AO reads ~1 on the flat, fully-exposed brick faces, drops to a
    mid ramp on the chamfered arris shoulders + the brick perimeter as it rolls
    into the recessed joint, and near 0 in the deep joints. Isolating that MID
    band therefore picks out exactly the exposed convex edges of each stone --
    where masonry abrades. A trapezoid ColorRamp turns the AO into that band
    mask; it is gated by the brick mask (mortar never wears) and ``strength``,
    then used to lighten the brick albedo toward ``wear_color`` (abraded, chalky
    stone) and nudge its roughness. Returns (albedo_socket, rough_socket)."""
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.location = (x, y)
    ramp.color_ramp.interpolation = 'B_SPLINE'
    e = ramp.color_ramp.elements
    e[0].position = max(0.0, ao_lo - band)
    e[0].color = (0.0, 0.0, 0.0, 1.0)
    e[1].position = ao_lo
    e[1].color = (1.0, 1.0, 1.0, 1.0)
    e2 = ramp.color_ramp.elements.new(ao_hi)
    e2.color = (1.0, 1.0, 1.0, 1.0)
    e3 = ramp.color_ramp.elements.new(min(1.0, ao_hi + band))
    e3.color = (0.0, 0.0, 0.0, 1.0)
    nt.links.new(ao_scalar, ramp.inputs["Fac"])

    # gate to bricks only, then scale by the wear strength -> 0..strength fac
    g = nt.nodes.new("ShaderNodeMath")
    g.operation = 'MULTIPLY'
    g.location = (x + 260, y)
    nt.links.new(ramp.outputs["Color"], g.inputs[0])
    nt.links.new(brick_fac, g.inputs[1])
    s = nt.nodes.new("ShaderNodeMath")
    s.operation = 'MULTIPLY'
    s.location = (x + 420, y)
    s.inputs[1].default_value = strength
    s.use_clamp = True
    nt.links.new(g.outputs[0], s.inputs[0])
    wear_fac = s.outputs[0]

    worn = nt.nodes.new("ShaderNodeMixRGB")
    worn.location = (x + 620, y + 120)
    worn.blend_type = 'MIX'
    worn.inputs["Color2"].default_value = (*wear_color, 1.0)
    nt.links.new(wear_fac, worn.inputs["Fac"])
    nt.links.new(albedo, worn.inputs["Color1"])
    out_alb = worn.outputs["Color"]

    out_rough = rough
    if rough is not None and rough_delta != 0.0:
        md = nt.nodes.new("ShaderNodeMath")
        md.operation = 'MULTIPLY'
        md.location = (x + 620, y - 160)
        md.inputs[1].default_value = rough_delta
        nt.links.new(wear_fac, md.inputs[0])
        ad = nt.nodes.new("ShaderNodeMath")
        ad.operation = 'ADD'
        ad.location = (x + 780, y - 160)
        ad.use_clamp = True
        nt.links.new(rough, ad.inputs[0])
        nt.links.new(md.outputs[0], ad.inputs[1])
        out_rough = ad.outputs[0]
    return out_alb, out_rough


def build_masonry_material(name, mask, normal, ao, height,
                           brick="limestone", mortar="mortar",
                           tile=(4.5, 2.6), field_m=2.6,
                           brick_tint=(0.30, 0.25, 0.18),
                           mortar_tint=(0.66, 0.63, 0.57),
                           brick_contrast=0.35, mortar_contrast=0.08,
                           brick_sat=1.35,
                           wear=0.5, wear_color=(0.62, 0.58, 0.50),
                           wear_ao=(0.45, 0.80), wear_band=0.12, wear_rough=0.12,
                           mask_hardness=0.9, ao_strength=0.6,
                           rough_brick=0.72, rough_mortar=0.94,
                           tint_map=None, tint_var=0.22, normal_strength=1.0,
                           disp_scale=0.0, rot_deg=0.0, root=FIELD_ROOT):
    """A tiling masonry material: limestone bricks + mortar joints selected by the
    baked mask, with the baked normal + AO, roughness from the material fields.

    mask/normal/ao/height -- the seamless baked wall maps (PNG paths); sampled at
        the ``tile`` (metres) scale so bricks come out real-size.
    brick/mortar          -- field material keys (assetsrc/materials/<k>/fields).
    field_m               -- metres one limestone/mortar field patch spans (kept
        large so the aperiodic field does not visibly wrap on a small object).
    brick_sat             -- brick saturation multiplier (>1 warms the near-neutral
        limestone field toward sandstone; 1.0 leaves it unchanged).
    wear                  -- edge-wear strength (0 disables). Derived from the AO
        map's mid band (exposed convex arrises), gated to bricks, it lightens the
        albedo toward ``wear_color`` and adds ``wear_rough`` roughness there.
        ``wear_ao`` = the (lo, hi) AO band counted as an edge; ``wear_band`` = its
        ramp softness.
    disp_scale>0          -- also drive true displacement from the height map.
    """
    def fld(k, ch):
        return os.path.join(root, k, "fields", f"{k}_{ch}_field.png")

    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (2600, 0)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (2300, 0)
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

    tc = nt.nodes.new("ShaderNodeTexCoord")
    tc.location = (-2200, 0)
    uv = tc.outputs["UV"]
    map_wall = _mapping(nt, -2000, 250, uv, tile[0], tile[1], rot_deg)
    map_field = _mapping(nt, -2000, -250, uv, field_m, field_m, rot_deg)

    # limestone + mortar albedo/roughness (field-scale), each contrast-boosted
    # then tinted (limestone darker/higher-contrast; mortar a distinct tone).
    b_alb = _tint(nt, -1400, 460, _contrast(
        nt, -1560, 460, _tex(nt, -1700, 460, fld(brick, "albedo"), "sRGB",
                             map_field).outputs["Color"], brick_contrast),
        brick_tint)
    # per-brick tint variation: a baked map giving each brick a random value,
    # remapped to a brightness multiplier so no two bricks are the same shade.
    if tint_map:
        tv = _tex(nt, -1700, 720, tint_map, "Non-Color", map_wall).outputs["Color"]
        tmr = nt.nodes.new("ShaderNodeMapRange")
        tmr.location = (-1400, 720)
        tmr.inputs["To Min"].default_value = 1.0 - tint_var
        tmr.inputs["To Max"].default_value = 1.0 + tint_var
        nt.links.new(tv, tmr.inputs["Value"])
        tmul = nt.nodes.new("ShaderNodeMixRGB")
        tmul.location = (-1150, 560)
        tmul.blend_type = 'MULTIPLY'
        tmul.inputs["Fac"].default_value = 1.0
        nt.links.new(b_alb, tmul.inputs["Color1"])
        nt.links.new(tmr.outputs["Result"], tmul.inputs["Color2"])
        b_alb = tmul.outputs["Color"]
    m_alb = _tint(nt, -1400, -120, _contrast(
        nt, -1560, -120, _tex(nt, -1700, -120, fld(mortar, "albedo"), "sRGB",
                              map_field).outputs["Color"], mortar_contrast),
        mortar_tint)
    b_r = _bw_scale(nt, -1400, 200,
                    _tex(nt, -1700, 200, fld(brick, "rough"), "Non-Color",
                         map_field).outputs["Color"], rough_brick)
    m_r = _bw_scale(nt, -1400, -380,
                    _tex(nt, -1700, -380, fld(mortar, "rough"), "Non-Color",
                         map_field).outputs["Color"], rough_mortar)

    # push brick saturation (limestone/mortar fields are near-neutral, so a plain
    # dark tint reads grey; boost chroma so the stone looks like warm sandstone).
    if brick_sat != 1.0:
        hsv = nt.nodes.new("ShaderNodeHueSaturation")
        hsv.location = (-1150, 360)
        hsv.inputs["Saturation"].default_value = brick_sat
        nt.links.new(b_alb, hsv.inputs["Color"])
        b_alb = hsv.outputs["Color"]

    # brick-vs-mortar selector from the baked mask (wall-scale, hardened binary)
    mask_c = _tex(nt, -1700, -640, mask, "Non-Color", map_wall).outputs["Color"]
    fac = _sharpen(nt, -1400, -640, mask_c, mask_hardness)   # 1 = brick, 0 = joint

    # baked AO, sampled once and reused for edge wear + the final AO multiply
    ao_c = _tex(nt, -900, 560, ao, "Non-Color", map_wall).outputs["Color"]

    # edge wear: abrade/lighten the brick albedo on the exposed convex edges the
    # AO map isolates as a mid-value band (mortar is excluded via the brick mask).
    if wear > 0.0:
        b_alb, b_r = _edge_wear(nt, -1150, 40, b_alb, ao_c, fac, wear_color,
                                wear, wear_ao[0], wear_ao[1], wear_band,
                                rough=b_r, rough_delta=wear_rough)

    color = _mix_color(nt, -900, 300, m_alb, b_alb, fac)
    rough = _mix_float(nt, -900, -250, m_r, b_r, fac)
    aomix = nt.nodes.new("ShaderNodeMixRGB")
    aomix.location = (-500, 400)
    aomix.blend_type = 'MULTIPLY'
    aomix.inputs["Fac"].default_value = ao_strength
    nt.links.new(color, aomix.inputs["Color1"])
    nt.links.new(ao_c, aomix.inputs["Color2"])
    nt.links.new(aomix.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(rough, bsdf.inputs["Roughness"])

    # baked mesoscale normal -> BSDF via a Normal Map node (the baked normal
    # already carries the micro relief, so no extra bump).
    n_img = _tex(nt, -900, -560, normal, "Non-Color", map_wall)
    nmap = nt.nodes.new("ShaderNodeNormalMap")
    nmap.location = (-620, -560)
    nmap.inputs["Strength"].default_value = normal_strength
    nt.links.new(n_img.outputs["Color"], nmap.inputs["Color"])
    nt.links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])

    # optional true displacement from the baked height
    if disp_scale > 0.0 and height:
        h_img = _tex(nt, 1900, -1050, height, "Non-Color", map_wall)
        disp = nt.nodes.new("ShaderNodeDisplacement")
        disp.location = (2200, -1050)
        disp.inputs["Scale"].default_value = disp_scale
        disp.inputs["Midlevel"].default_value = 0.5
        nt.links.new(h_img.outputs["Color"], disp.inputs["Height"])
        nt.links.new(disp.outputs["Displacement"], out.inputs["Displacement"])
        mat.cycles.displacement_method = 'BOTH'
    return mat
