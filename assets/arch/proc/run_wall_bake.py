"""Build a running-bond hewn-brick wall and bake it to tileable material maps.

Thin CPU runner over ``bake_wall.bake_wall`` (builder ``build_wall``, brick
prefabs). Headless::

    blender --background --factory-startup --python assets/arch/proc/run_wall_bake.py

Env: WALL_W/WALL_H (m), WALL_RES, WALL_SEED, WALL_DEVICE (default CPU), WALL_OUT.
"""
import os

import bpy  # noqa: F401  (ensures we are inside Blender)

AP = os.path.dirname(os.path.abspath(__file__))
W = float(os.environ.get("WALL_W", "4.0"))
H = float(os.environ.get("WALL_H", "4.0"))
RES = int(os.environ.get("WALL_RES", "2048"))
SEED = int(os.environ.get("WALL_SEED", "4"))
DEVICE = os.environ.get("WALL_DEVICE", "CPU")
OUT = os.environ.get("WALL_OUT", os.path.join(AP, "prefabs", "bake"))

ns = {"__file__": os.path.join(AP, "bake_wall.py")}
with open(os.path.join(AP, "bake_wall.py")) as f:
    exec(compile(f.read(), "bake_wall.py", "exec"), ns)

paths = ns["bake_wall"](width=W, height=H, res=RES, seed=SEED, out_dir=OUT,
                        device=DEVICE, name="brick_wall", builder="build_wall")
print("[wall] baked ->", OUT, flush=True)
for k, v in paths.items():
    print(f"  {k}: {v}", flush=True)
