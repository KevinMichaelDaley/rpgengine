"""Build a square dressed-stone paved FLOOR from the tile prefabs and bake it to
tileable ground material maps (mask / tint / height / normal / ao).

Thin runner over ``bake_wall.bake_wall`` with ``builder="build_floor"`` -- the
floor lays the ``prefabs/tiles`` set in the X-Z plane facing the front-ortho bake
camera, so the identical wall bake pipeline captures it "straight down" onto the
paving.

Headless (heavy: run on the GPU/bake box, CPU Cycles for the big many-tile
scenes)::

    blender --background --factory-startup \
        --python assets/arch/proc/run_floor_bake.py

Environment overrides:
    FLOOR_W / FLOOR_H   floor extent in metres (default 3.0 x 3.0)
    FLOOR_RES           bake resolution, long side (default 4096)
    FLOOR_SEED          RNG seed (default 5)
    FLOOR_DEVICE        GPU | CPU (default CPU -- many instances)
    FLOOR_OUT           output dir (default assets/arch/proc/prefabs/bake_floor)
"""
import os

import bpy

AP = os.path.dirname(os.path.abspath(__file__))

W = float(os.environ.get("FLOOR_W", "3.0"))
H = float(os.environ.get("FLOOR_H", "3.0"))
RES = int(os.environ.get("FLOOR_RES", "4096"))
SEED = int(os.environ.get("FLOOR_SEED", "5"))
DEVICE = os.environ.get("FLOOR_DEVICE", "CPU")
OUT = os.environ.get("FLOOR_OUT", os.path.join(AP, "prefabs", "bake_floor"))

# Pin __file__ so bake_wall resolves its sibling brick_wall.py from THIS checkout.
ns = {"__file__": os.path.join(AP, "bake_wall.py")}
with open(os.path.join(AP, "bake_wall.py")) as f:
    exec(compile(f.read(), "bake_wall.py", "exec"), ns)

paths = ns["bake_wall"](
    width=W, height=H, res=RES, seed=SEED, out_dir=OUT, device=DEVICE,
    name="stone_floor", builder="build_floor",
    build_kw={
        # Clean square paving, hand-set look, uneven settled height.
        "gap": 0.006, "tile_aspect": 1, "offset_jitter": 0.0,
        "rot_deg": 1.0, "depth_var": 0.02,
    },
)
print("[floor] baked ->", OUT, flush=True)
for k, v in paths.items():
    print(f"  {k}: {v}", flush=True)
