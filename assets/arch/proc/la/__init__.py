"""Dystopian-LA architectural generators (epic rpg-2lyk).

Package layout (see ref/archgen_dystopian_la.md):
  topology.py  -- production-topology validation (quality bar, doc section 0)
  params.py    -- spec-driven redo-panel operator factory + tool registry
  menu.py      -- Add > Dystopian LA menu tree built from the registry
  tools/       -- one module per generator family (A..G), each registering
                  its build_* functions through params.register_tool()

Everything here obeys the MODELING QUALITY BAR: no blockouts, all-quad
topology, no T-junctions, deliberate edge flow, ASCII topology plans in tool
docstrings, programmatic validation in every smoke check, and interactive
wireframe sign-off per tool ticket.

Usage inside Blender (via the MCP bridge or the text editor):

    import sys; sys.path.append("/home/kmd/rpg/assets/arch/proc")
    import la; la.register()
"""
from . import params
from . import menu


def register():
    """Register every declared tool operator + the menu tree."""
    params.register_operators()
    menu.register()


def unregister():
    menu.unregister()
    params.unregister_operators()
