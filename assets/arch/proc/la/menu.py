"""Add > Dystopian LA menu tree, generated from the tool registry (rpg-gh2z).

Families appear in FAMILY_ORDER; tools appear in declaration order within
each family. Registration is idempotent (safe to re-run through the MCP
bridge while iterating).
"""
import bpy

from . import params

FAMILY_ORDER = [
    "Residential", "Commercial", "Infrastructure", "Streetscape",
    "Decay", "Aberration", "Assembly",
]

_MENU_CLASSES = []


class VIEW3D_MT_ferrum_la(bpy.types.Menu):
    bl_idname = "VIEW3D_MT_ferrum_la"
    bl_label = "Dystopian LA"

    def draw(self, context):
        del context
        present = {t["family"] for t in params.registry().values()}
        for fam in FAMILY_ORDER:
            if fam in present:
                self.layout.menu("VIEW3D_MT_ferrum_la_" + fam.lower())


def _family_menu(family):
    def draw(self, context):
        del context
        for tool in params.registry().values():
            if tool["family"] == family:
                self.layout.operator("ferrum." + tool["idname"],
                                     text=tool["label"])
    return type("VIEW3D_MT_ferrum_la_" + family.lower(), (bpy.types.Menu,),
                {"bl_idname": "VIEW3D_MT_ferrum_la_" + family.lower(),
                 "bl_label": family, "draw": draw})


def _add_menu_entry(self, context):
    del context
    self.layout.menu("VIEW3D_MT_ferrum_la", icon='HOME')


def _scrub_stale():
    """Remove EVERY prior registration by NAME -- module reloads orphan the
    old function/class objects, so holding references is not enough (the
    user-visible symptom: one 'Dystopian LA' entry per reload)."""
    draw_funcs = getattr(bpy.types.VIEW3D_MT_add.draw, "_draw_funcs", None)
    if draw_funcs is not None:
        for fn in [f for f in draw_funcs if getattr(f, "__name__", "") ==
                   "_add_menu_entry"]:
            bpy.types.VIEW3D_MT_add.remove(fn)
    names = ["VIEW3D_MT_ferrum_la"] +             ["VIEW3D_MT_ferrum_la_" + f.lower() for f in FAMILY_ORDER]
    for n in names:
        cls = getattr(bpy.types, n, None)
        if cls is not None:
            try:
                bpy.utils.unregister_class(cls)
            except Exception:
                pass


def register():
    _scrub_stale()
    _MENU_CLASSES.clear()
    _MENU_CLASSES.append(VIEW3D_MT_ferrum_la)
    for fam in FAMILY_ORDER:
        _MENU_CLASSES.append(_family_menu(fam))
    for cls in _MENU_CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.VIEW3D_MT_add.append(_add_menu_entry)


def unregister():
    _scrub_stale()
    _MENU_CLASSES.clear()
