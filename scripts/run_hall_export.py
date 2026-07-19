"""Headless: regenerate the great-hall scene with the EXISTING generator and
export it to the engine (FVMA meshes + baked PBR materials + scene manifest).

The scene is rebuilt by ``great_hall.build_hall_scene`` (passed as the exporter's
regeneration callback), so this runs from an empty .blend -- no hand-set-up scene
needed -- and records every mesh's placement straight from Blender.

    blender --background --factory-startup --python scripts/run_hall_export.py

Env: HALL_OUT (output dir), HALL_BAKE_RES (material bake resolution, default 1024).
"""
import os
import sys

import bpy

REPO = "/home/kmd/rpg"
AP = os.path.join(REPO, "assets", "arch", "proc")
SC = os.path.join(REPO, "scripts")
for p in (AP, SC):
    if p not in sys.path:
        sys.path.insert(0, p)

# Empty the factory scene so only the regenerated hall is exported.
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)

import great_hall  # noqa: E402  (the existing geometry + material generator)
import export_scene  # noqa: E402

BAKE_ROOT = os.path.join(AP, "prefabs")
OUT = os.environ.get("HALL_OUT", os.path.join(REPO, "datasets", "great_hall_export"))
RES = int(os.environ.get("HALL_BAKE_RES", "1024"))

# HALL_BAKE_MATERIALS=0 skips the slow Cycles material bake (reuse on-disk maps).
BAKE_MATERIALS = os.environ.get("HALL_BAKE_MATERIALS", "1") not in ("0", "false", "")

export_scene.export_scene(
    "great_hall", OUT, bake_res=RES, bake_materials=BAKE_MATERIALS,
    scene_callback=lambda: great_hall.build_hall_scene(BAKE_ROOT))
print("[run_hall_export] done ->", OUT, flush=True)
