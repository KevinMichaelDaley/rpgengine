#!/usr/bin/env python3
"""
gen_material_seeds.py — Generate flat, tileable PBR *seed* textures for the
procedural Romanesque material library (tickets rpg-lb1q / rpg-ljxt) via
OpenRouter image models.

These are NOT finished PBR textures. They are deliberately FLAT, evenly-lit,
mid-neutral, low-contrast detail sources that behave like AI noise/pattern
textures tuned to a target material (grain, veining, mortar speckle, plaster
mottle, patina). The Blender node graph (ticket rpg-lbky) randomly samples and
blends several of them, tinted/roughened by the PBR params from
ref/romanesque_materials.md, to build the final base maps.

For each material we generate two channel types:
  - albedo : subtle surface colour/value detail (mid-neutral; tint added later)
  - rough  : grayscale micro-surface / roughness detail (light=rough, dark=smooth)

--repeats controls how many variant images per (material, channel) — the node
graph samples across variants for per-instance variation. The run is resumable:
an existing output file is skipped. Output layout:

    assetsrc/materials/<material>/<material>_<channel>_<rep>.png

Usage:
    export OPENROUTER_API_KEY=$(cat ~/.ssh/OPENROUTER_API_KEY)
    python3 scripts/gen_material_seeds.py --out assetsrc/materials --repeats 3
    # subset / test batch:
    python3 scripts/gen_material_seeds.py --out assetsrc/materials \
        --materials limestone,marble,plaster --channels albedo,rough --repeats 2
    python3 scripts/gen_material_seeds.py --dry-run --limit 10 ...
"""

import argparse
import base64
import glob
import json
import os
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

import requests

OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"
DEFAULT_MODEL = "google/gemini-3.1-flash-lite-image"  # cheapest, ~$0.034/img

# System prompt: enforces the flat, seamless, mid-neutral "seed" style shared by
# every material and channel. This is the single most important lever for
# keeping the outputs sample-able rather than finished/lit textures.
SYSTEM_PROMPT = (
    "You generate FLAT, seamless, tileable texture detail maps used as "
    "procedural material seeds in a physically-based (PBR) shader. Output a "
    "single square image that tiles seamlessly with no visible seam at the "
    "edges. The surface is lit by perfectly FLAT, even, shadowless light, like "
    "a flatbed scan or a raw albedo map: NO baked highlights, NO cast shadows, "
    "NO directional lighting, NO ambient occlusion, NO vignetting, NO 3D relief "
    "shading, NO gloss reflections. Fill the ENTIRE frame edge to edge with the "
    "surface only: no border, no frame, no object, no background, no props, no "
    "text, no watermark, no colour swatches. Keep contrast LOW and the overall "
    "tone MID-NEUTRAL — this is a detail source that gets tinted later, not a "
    "finished texture. Straight top-down orthographic, evenly exposed, "
    "photoreal microdetail."
)

ALBEDO_SUFFIX = (
    " Flat albedo detail only, mostly mid-grey/neutral with subtle tonal "
    "variation; muted, desaturated; even exposure across the whole frame."
)
ROUGH_SUFFIX = (
    " Render this as a GRAYSCALE micro-surface roughness map: no colour, "
    "lighter = rougher/matte, darker = smoother; show the surface's fine "
    "structure as low-contrast grayscale variation around mid-grey."
)

# Per-material albedo prompts. Each describes the flat surface pattern, drawn
# from the seed notes in ref/romanesque_materials.md.
MATERIALS = {
    "limestone": "Dressed pale buff limestone ashlar surface: fine even stone "
        "grain, faint parallel claw-tooth chisel tooling striations, a few "
        "small darker mineral inclusions, very subtle mottling.",
    "sandstone": "Dressed tan sandstone surface: fine granular grain with "
        "gentle horizontal sedimentary bedding lines and faint iron-oxide "
        "banding, slightly porous.",
    "travertine": "Cream travertine / tufa surface: smooth pale stone with "
        "scattered small elongated pits and voids clustered in bands.",
    "granite": "Honed grey granite surface: dense crystalline speckle of grey, "
        "white, black and faint pink mineral flecks evenly distributed, no "
        "directional structure.",
    "flint": "Knapped flint set in pale lime mortar: irregular rounded dark "
        "glassy grey-black nodules with pale chalky cortex rims, in a light "
        "mortar matrix.",
    "marble": "Pale honed marble surface: soft cloudy off-white ground with a "
        "gentle branching network of faint grey veins, very low contrast.",
    "brick": "Fired red-brown brickwork: fine clay grain with subtle brick-to-"
        "brick colour variation and occasional darker fire-flash, thin mortar "
        "joints in a running bond.",
    "terracotta": "Fired terracotta surface: smooth warm orange-red clay with "
        "fine surface pitting and subtle mottling.",
    "plaster": "Old lime plaster / fresco ground: off-white cream matte plaster "
        "with broad soft mottling and faint trowel sweep arcs, almost no high-"
        "frequency detail.",
    "bronze": "Burnished bronze with age: warm gold-brown metal showing fine "
        "brushed micro-scratches and blotchy green-brown patina in low areas.",
    "iron": "Wrought iron surface: dark near-black low-saturation metal with "
        "hammer-planished dimples and streaky lighter wear marks.",
    "gold": "Gold leaf / gilding: warm yellow gold with a very fine slightly "
        "wrinkled leaf surface, faint micro-cracks.",
    "oak": "Aged oak timber surface: mid-brown wood with directional grain "
        "lines, ray fleck, and an occasional small knot; faint adze marks.",
}

CHANNELS = {"albedo": ALBEDO_SUFFIX, "rough": ROUGH_SUFFIX}


def build_prompt(material_desc: str, channel: str) -> str:
    """Compose the per-image user prompt: material surface + channel suffix."""
    return material_desc + CHANNELS[channel]


class RateLimit:
    """Thread-safe minimum-interval throttle between request starts."""

    def __init__(self, min_interval_s: float):
        self._min = min_interval_s
        self._lock = threading.Lock()
        self._next_ok = 0.0

    def wait(self):
        with self._lock:
            now = time.monotonic()
            if now < self._next_ok:
                time.sleep(self._next_ok - now)
                now = time.monotonic()
            self._next_ok = now + self._min


def _ext_for_mime(data_url: str) -> str:
    """Map the data-URL mime type to a file extension (png/webp/jpg)."""
    head = data_url[:32].lower()
    if "image/png" in head:
        return "png"
    if "image/webp" in head:
        return "webp"
    return "jpg"


def generate_one(session, api_key, model, system_prompt, user_prompt,
                 out_base, ratelimit, max_retries=4, timeout=240):
    """Generate the image(s) for one prompt. Returns (ok, note, saved, cost)."""
    headers = {
        "Authorization": f"Bearer {api_key}",
        "Content-Type": "application/json",
        "HTTP-Referer": "https://github.com/ferrum-engine/rpg",
        "X-Title": "Ferrum Material Seeds",
    }
    body = {
        "model": model,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_prompt},
        ],
        "modalities": ["image", "text"],
        "usage": {"include": True},
    }

    backoff = 3.0
    for attempt in range(1, max_retries + 1):
        ratelimit.wait()
        try:
            resp = session.post(OPENROUTER_URL, headers=headers,
                                 json=body, timeout=timeout)
        except requests.RequestException as exc:
            if attempt < max_retries:
                time.sleep(backoff)
                backoff *= 2
                continue
            return False, f"network error: {exc}", [], 0.0

        if resp.status_code == 429 or resp.status_code >= 500:
            if attempt < max_retries:
                time.sleep(backoff)
                backoff *= 2
                continue
            return False, f"http {resp.status_code}: {resp.text[:200]}", [], 0.0

        if resp.status_code != 200:
            return False, f"http {resp.status_code}: {resp.text[:200]}", [], 0.0

        try:
            data = resp.json()
        except ValueError:
            return False, "non-JSON response", [], 0.0

        if data.get("error"):
            msg = str(data["error"])[:200]
            if attempt < max_retries and ("rate" in msg.lower()
                                          or "overload" in msg.lower()):
                time.sleep(backoff)
                backoff *= 2
                continue
            return False, f"api error: {msg}", [], 0.0

        try:
            images = data["choices"][0]["message"].get("images") or []
        except (KeyError, IndexError):
            images = []

        if not images:
            if attempt < max_retries:
                time.sleep(backoff)
                backoff *= 2
                continue
            return False, "no image in response", [], 0.0

        cost = float(data.get("usage", {}).get("cost", 0.0) or 0.0)
        saved = []
        total_bytes = 0
        for k, img in enumerate(images):
            url = img.get("image_url", {}).get("url", "")
            if "," not in url:
                continue
            try:
                raw = base64.b64decode(url.split(",", 1)[1])
            except Exception:  # noqa: BLE001 - skip a bad image, keep the rest
                continue
            suffix = "" if k == 0 else f"_{chr(ord('b') + k - 1)}"
            out_path = f"{out_base}{suffix}.{_ext_for_mime(url)}"
            tmp_path = out_path + ".part"
            with open(tmp_path, "wb") as fh:
                fh.write(raw)
            os.replace(tmp_path, out_path)  # atomic: partials never look done
            saved.append(os.path.basename(out_path))
            total_bytes += len(raw)

        if not saved:
            return False, "images present but none decoded", [], cost
        return True, f"{len(saved)} img, {total_bytes} bytes", saved, cost

    return False, "exhausted retries", [], 0.0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="assetsrc/materials",
                    help="Output root; images go in <out>/<material>/")
    ap.add_argument("--materials", default="",
                    help="Comma-separated subset (default: all)")
    ap.add_argument("--channels", default="albedo,rough",
                    help="Comma-separated channels: albedo,rough")
    ap.add_argument("--repeats", type=int, default=3,
                    help="Variant images per (material, channel)")
    ap.add_argument("--workers", type=int, default=6, help="Concurrent workers")
    ap.add_argument("--model", default=DEFAULT_MODEL, help="OpenRouter model id")
    ap.add_argument("--limit", type=int, default=0,
                    help="Stop after this many NEW images (0 = no limit)")
    ap.add_argument("--min-interval", type=float, default=0.15,
                    help="Minimum seconds between request starts")
    ap.add_argument("--dry-run", action="store_true",
                    help="List the work without calling the API")
    args = ap.parse_args()

    api_key = os.environ.get("OPENROUTER_API_KEY", "").strip()
    if not api_key and not args.dry_run:
        key_file = os.path.expanduser("~/.ssh/OPENROUTER_API_KEY")
        if os.path.exists(key_file):
            api_key = open(key_file).read().strip()
    if not api_key and not args.dry_run:
        print("ERROR: set OPENROUTER_API_KEY (or ~/.ssh/OPENROUTER_API_KEY)",
              file=sys.stderr)
        return 2

    wanted = [m.strip() for m in args.materials.split(",") if m.strip()] \
        or list(MATERIALS.keys())
    unknown = [m for m in wanted if m not in MATERIALS]
    if unknown:
        print(f"ERROR: unknown materials: {unknown}\n"
              f"known: {', '.join(MATERIALS)}", file=sys.stderr)
        return 2
    channels = [c.strip() for c in args.channels.split(",") if c.strip()]
    bad = [c for c in channels if c not in CHANNELS]
    if bad:
        print(f"ERROR: unknown channels: {bad} (known: albedo, rough)",
              file=sys.stderr)
        return 2

    # Build the task list: material x channel x repeats, one API call each.
    tasks = []
    skipped = 0
    for material in wanted:
        mat_dir = os.path.join(args.out, material)
        for channel in channels:
            prompt = build_prompt(MATERIALS[material], channel)
            for rep in range(args.repeats):
                base_name = f"{material}_{channel}_{rep:02d}"
                out_base = os.path.join(mat_dir, base_name)
                existing = [p for p in glob.glob(out_base + "*")
                            if not p.endswith(".part")]
                if existing:
                    skipped += 1
                    continue
                tasks.append((material, channel, prompt, base_name, out_base,
                              mat_dir))

    total = len(wanted) * len(channels) * args.repeats
    if args.limit > 0:
        tasks = tasks[:args.limit]

    per_call_est = 0.034
    print(f"Materials:       {len(wanted)} ({', '.join(wanted)})")
    print(f"Channels:        {', '.join(channels)}")
    print(f"Repeats:         {args.repeats}")
    print(f"Target images:   {total}")
    print(f"Already present: {skipped}")
    print(f"To generate:     {len(tasks)} images")
    print(f"Model:           {args.model}")
    print(f"Est. cost:       ~${len(tasks) * per_call_est:,.2f}")
    print(f"Output root:     {args.out}")

    if args.dry_run:
        for t in tasks[:12]:
            print(f"  would gen {t[3]}: {t[2][:90]}")
        if len(tasks) > 12:
            print(f"  ... and {len(tasks) - 12} more")
        return 0
    if not tasks:
        print("Nothing to do — all images already exist.")
        return 0

    ratelimit = RateLimit(args.min_interval)
    meta_lock = threading.Lock()
    counters = {"ok": 0, "fail": 0, "images": 0, "cost": 0.0}
    counter_lock = threading.Lock()
    start = time.monotonic()

    def worker(task):
        material, channel, prompt, base_name, out_base, mat_dir = task
        os.makedirs(mat_dir, exist_ok=True)
        session = requests.Session()
        ok, note, saved, cost = generate_one(
            session, api_key, args.model, SYSTEM_PROMPT, prompt,
            out_base, ratelimit)
        if ok:
            with meta_lock:
                with open(os.path.join(args.out, "metadata.jsonl"), "a",
                          encoding="utf-8") as fh:
                    for fn in saved:
                        fh.write(json.dumps({
                            "file": f"{material}/{fn}",
                            "material": material,
                            "channel": channel,
                            "prompt": prompt,
                            "model": args.model,
                        }) + "\n")
        with counter_lock:
            counters["ok" if ok else "fail"] += 1
            counters["images"] += len(saved)
            counters["cost"] += cost
            done = counters["ok"] + counters["fail"]
            imgs, spent = counters["images"], counters["cost"]
        status = "OK " if ok else "FAIL"
        print(f"[{done}/{len(tasks)}] {status} {base_name} ({note}) "
              f"| {imgs} imgs | ${spent:.2f}", flush=True)
        return ok

    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = [pool.submit(worker, t) for t in tasks]
        for _ in as_completed(futures):
            pass

    elapsed = time.monotonic() - start
    print(f"\nDone. {counters['ok']} ok, {counters['fail']} failed "
          f"({counters['images']} images) in {elapsed/60:.1f} min. "
          f"Spent ~${counters['cost']:.2f}.")
    return 0 if counters["fail"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
