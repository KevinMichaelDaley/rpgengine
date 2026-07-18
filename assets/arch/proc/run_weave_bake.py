"""Build a toothed window-splay reveal weave and bake it to tileable maps.

Thin CPU runner over ``bake_wall.bake_wall`` (builder ``build_weave``, brick
prefabs -- the whole set, so the weave does not repeat trivially). Headless::

    blender --background --factory-startup --python assets/arch/proc/run_weave_bake.py

Env: WEAVE_W/WEAVE_H (m), WEAVE_RES, WEAVE_SEED, WEAVE_DEVICE (default CPU),
WEAVE_OUT.
"""
import os

import bpy  # noqa: F401

AP = os.path.dirname(os.path.abspath(__file__))
W = float(os.environ.get("WEAVE_W", "1.2"))
H = float(os.environ.get("WEAVE_H", "1.0"))
RES = int(os.environ.get("WEAVE_RES", "2048"))
SEED = int(os.environ.get("WEAVE_SEED", "7"))
DEVICE = os.environ.get("WEAVE_DEVICE", "CPU")
OUT = os.environ.get("WEAVE_OUT", os.path.join(AP, "prefabs", "bake_weave"))

ns = {"__file__": os.path.join(AP, "bake_wall.py")}
with open(os.path.join(AP, "bake_wall.py")) as f:
    exec(compile(f.read(), "bake_wall.py", "exec"), ns)

paths = ns["bake_wall"](width=W, height=H, res=RES, seed=SEED, out_dir=OUT,
                        device=DEVICE, name="brick_weave", builder="build_weave")
print("[weave] baked ->", OUT, flush=True)
for k, v in paths.items():
    print(f"  {k}: {v}", flush=True)
