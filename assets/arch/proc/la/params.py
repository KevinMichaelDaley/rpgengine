"""Spec-driven redo-panel operators + the tool registry (rpg-gh2z).

One declarative PARAM SPEC per tool is the single source of truth for the
build function's defaults/ranges AND the operator's redo-panel properties —
so the script/MCP path and the UI path can never drift apart.

A spec is a list of dicts:

    SPEC = [
        dict(name="width", type='FLOAT', default=14.0, min=8.0, max=22.0,
             unit='LENGTH', desc="Building width"),
        dict(name="floors", type='INT', default=2, min=1, max=3),
        dict(name="awnings", type='BOOL', default=True),
        dict(name="facade_style", type='ENUM', default='starburst',
             items=('plain', 'starburst', 'mansard', 'tiki', 'script')),
        dict(name="address_text", type='STRING', default="4623"),
    ]

Every tool implicitly gets ``seed: INT`` (deterministic reroll). Tools call

    register_tool(idname="la_dingbat", label="Dingbat Apartment",
                  family="Residential", build=build_dingbat, spec=SPEC)

and ``register_operators()`` turns the registry into ``bpy.types.Operator``
subclasses (``ferrum.la_dingbat``) whose ``execute()`` is PURE: it builds
fresh objects at the 3D cursor into a per-invocation ``LA_<Tool>`` collection,
never touching prior invocations (undo/redo owns regeneration).

The build callable's contract:

    build(params: dict, rng: random.Random) -> list[bpy.types.Object]

Objects come back UNLINKED; the operator links, parents, places, selects.
"""
import random

import bpy

#: Shared spec entries (rules 1 + 3): every building tool includes MODE_PARAM;
#: story options are off-by-default BoolProperties declared LAST in the spec so
#: they group at the bottom of the redo panel.
MODE_PARAM = dict(name="mode", type='ENUM', default='facade',
                  items=('facade', 'interior'),
                  desc="facade = shell only; interior = all structural walls/"
                       "slabs/columns, just-built and walkable")

#: Shared vertex-group vocabulary (rule 5). Tools pick from these names.
VGROUPS = ("facade_front", "facade_back", "facade_side", "windows", "doors",
           "parapet", "roof", "carport", "steps", "awnings", "ac_units",
           "interior_walls", "partitions", "slabs", "columns", "walkway",
           "loggia", "story", "storefront", "shutters", "signage", "canopy",
           "corridor", "demising", "lot", "lot_lines", "pole_sign",
           "road", "gutter", "curb", "sidewalk", "median", "paint",
           "patches", "sinkhole")

#: idname -> dict(idname, label, family, build, spec). Declaration order is
#: preserved and drives menu order within a family.
_REGISTRY = {}
#: Registered operator classes (for unregister).
_OP_CLASSES = []

_PROP_BUILDERS = {
    'FLOAT': lambda e: bpy.props.FloatProperty(
        name=e.get("label", e["name"].replace("_", " ").title()),
        description=e.get("desc", ""), default=e.get("default", 0.0),
        min=e.get("min", -1e9), max=e.get("max", 1e9),
        soft_min=e.get("min", 0.0), soft_max=e.get("max", 1.0),
        unit=e.get("unit", 'NONE')),
    'INT': lambda e: bpy.props.IntProperty(
        name=e.get("label", e["name"].replace("_", " ").title()),
        description=e.get("desc", ""), default=e.get("default", 0),
        min=e.get("min", -2**30), max=e.get("max", 2**30),
        soft_min=e.get("min", 0), soft_max=e.get("max", 10)),
    'BOOL': lambda e: bpy.props.BoolProperty(
        name=e.get("label", e["name"].replace("_", " ").title()),
        description=e.get("desc", ""), default=e.get("default", False)),
    'ENUM': lambda e: bpy.props.EnumProperty(
        name=e.get("label", e["name"].replace("_", " ").title()),
        description=e.get("desc", ""),
        items=[(i, i.replace("_", " ").title(), "") for i in e["items"]],
        default=e.get("default", e["items"][0])),
    'STRING': lambda e: bpy.props.StringProperty(
        name=e.get("label", e["name"].replace("_", " ").title()),
        description=e.get("desc", ""), default=e.get("default", "")),
}


def register_tool(idname, label, family, build, spec,
                  needs_context=False, at_cursor=True):
    """Declare a tool. Call at module import; idempotent by idname.

    needs_context: build is called as build(params, rng, context) and may
    read/modify the selection (e.g. the road-network operator consumes the
    active edge-skin mesh). at_cursor=False: the build returns objects in
    world coordinates; linking skips the cursor offset."""
    _REGISTRY[idname] = dict(idname=idname, label=label, family=family,
                             build=build, spec=list(spec),
                             needs_context=needs_context,
                             at_cursor=at_cursor)


def registry():
    """The declaration-ordered tool registry (read-only view)."""
    return dict(_REGISTRY)


def defaults(idname):
    """The tool's parameter defaults as a plain dict (script-path entry)."""
    t = _REGISTRY[idname]
    d = {e["name"]: e.get("default") for e in t["spec"]}
    d["seed"] = 0
    return d


def run_tool(idname, **overrides):
    """Script/MCP entry: build with defaults + overrides, link + place like
    the operator would, return the objects. Deterministic per seed."""
    t = _REGISTRY[idname]
    p = defaults(idname)
    p.update(overrides)
    rng = random.Random(p["seed"])
    if t.get("needs_context"):
        objects = t["build"](p, rng, bpy.context)
    else:
        objects = t["build"](p, rng)
    return _link_result(t, p, objects)


def _link_result(tool, params, objects):
    """Shared linking: fresh LA_<Tool> collection, cursor placement, select."""
    del params
    name = "LA_" + tool["idname"].replace("la_", "", 1).title()
    coll = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(coll)
    cursor = bpy.context.scene.cursor.location.copy()
    for ob in objects:
        coll.objects.link(ob)
        if tool.get("at_cursor", True):
            ob.location = ob.location + cursor
    for ob in bpy.context.selected_objects:
        ob.select_set(False)
    for ob in objects:
        ob.select_set(True)
    if objects:
        bpy.context.view_layer.objects.active = objects[0]
    return objects


def _make_operator(tool):
    """Build the Operator subclass for one registry entry."""
    props = {"seed": bpy.props.IntProperty(name="Seed", default=0, min=0,
                                           description="Deterministic reroll")}
    for entry in tool["spec"]:
        props[entry["name"]] = _PROP_BUILDERS[entry["type"]](entry)

    def execute(self, context):
        p = {e["name"]: getattr(self, e["name"]) for e in tool["spec"]}
        p["seed"] = self.seed
        try:
            if tool.get("needs_context"):
                objects = tool["build"](p, random.Random(self.seed), context)
            else:
                objects = tool["build"](p, random.Random(self.seed))
        except ValueError as exc:
            self.report({'ERROR'}, str(exc))
            return {'CANCELLED'}
        _link_result(tool, p, objects)
        return {'FINISHED'}

    cls = type(
        "FERRUM_OT_" + tool["idname"],
        (bpy.types.Operator,),
        {
            "bl_idname": "ferrum." + tool["idname"],
            "bl_label": tool["label"],
            "bl_options": {'REGISTER', 'UNDO'},   # UNDO => redo panel.
            "__annotations__": props,
            "execute": execute,
        },
    )
    return cls


def register_operators():
    """Reload-proof: unregister any prior class of the same RNA name FIRST
    (after a module reload the old class object is orphaned -- holding
    references cannot clean it up)."""
    for tool in _REGISTRY.values():
        stale = getattr(bpy.types, "FERRUM_OT_" + tool["idname"], None)
        if stale is not None:
            try:
                bpy.utils.unregister_class(stale)
            except Exception:
                pass
        cls = _make_operator(tool)
        bpy.utils.register_class(cls)
        _OP_CLASSES.append(cls)


def unregister_operators():
    for cls in reversed(_OP_CLASSES):
        try:
            bpy.utils.unregister_class(cls)
        except Exception:
            pass
    _OP_CLASSES.clear()
