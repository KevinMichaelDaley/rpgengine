#!/usr/bin/env python3
"""
gen_dungeon_images.py — Generate a dataset of clean architectural-whitebox
dungeon room renders via OpenRouter image models.

Reads a text file of terse room descriptions (one per line), and for each room
renders a FIXED set of canonical architectural views (top-down plan, isometric,
front elevation, cutaway perspective), --repeats times each, saving one image
per API call. The system prompt enforces the empty untextured whitebox style
(no props, no lighting effects, flat grey, plain background).

The run is *resumable*: images whose output file already exists are skipped, so
you can stop and restart without regenerating (or re-paying for) prior images.
A metadata JSONL sidecar records the prompt behind every image.

Usage:
    export OPENROUTER_API_KEY=$(cat ~/.ssh/OPENROUTER_API_KEY)
    python3 scripts/gen_dungeon_images.py \
        --prompts datasets/dungeon/dwarven_prompts.txt \
        --system  datasets/dungeon/system_prompt.txt \
        --out     datasets/dungeon/dwarven \
        --repeats 5 \
        --workers 8

Total images = rooms x fixed-views x repeats (e.g. 50 x 4 x 5 = 1000).
"""

import argparse
import base64
import glob
import json
import os
import re
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

import requests

OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"
DEFAULT_MODEL = "google/gemini-3.1-flash-lite-image"  # cheap, clean whitebox

# Named view sets, selected with --view-set. Every room is rendered from each view
# in the chosen set, --repeats times. Each entry is (key, description).
VIEW_SETS = {
    # Canonical architectural drawing set: consistent multi-angle coverage.
    "arch": [
        ("plan", "Orthographic top-down floor plan, camera pointing straight down, "
                 "no perspective, showing the full room outline, walls, pillars and doorways."),
        ("iso", "Clean isometric 3D view of the entire room seen from above at a "
                "45-degree angle, showing the floor plan together with the wall heights."),
        ("elev", "Straight-on orthographic front elevation, looking directly into the "
                 "open front of the room to show the interior back wall and pillars."),
        ("persp", "Wide three-quarter cutaway perspective from a high corner with the "
                  "near walls removed, showing the floor, the far walls and the pillars."),
    ],
    # Single fixed Diablo-style orthographic 2D game-camera view.
    "diablo": [
        ("ortho", "Present the room as a flat 2D orthographic game render in the fixed "
                  "high three-quarter isometric camera angle of the classic action-RPG "
                  "Diablo, using parallel projection with no perspective distortion. "
                  "No UI, HUD, orbs, minimap, text, or overlays of any kind."),
    ],
}

# Restated hard constraints appended to every prompt to keep the model on the
# architectural-whitebox style (no props, no lighting, flat grey, plain background).
ARCH_CONSTRAINTS = (
    "Untextured flat light-grey whitebox architecture only. Flat even neutral "
    "lighting like a 3D-modeling viewport — no torches, colored light, glow or "
    "dramatic shadows. Completely empty room: no props, furniture, or clutter. "
    "Clean walls and pillars, not chunky or blocky; archways are fine. No "
    "staircases or steps. Doorways are open passages with no doors or gates in "
    "them. Plain white background."
)


def slugify(text: str, maxlen: int = 48) -> str:
    """Turn a room description into a short filesystem-safe slug."""
    text = text.lower()
    text = re.sub(r"[^a-z0-9]+", "_", text)
    text = text.strip("_")
    return text[:maxlen].rstrip("_")


def build_view_prompt(room: str, view_desc: str) -> str:
    """Compose the per-image user prompt: room + fixed view + constraints."""
    return f"{room}. {view_desc} {ARCH_CONSTRAINTS}"


def load_prompts(path: str) -> list:
    """Read room descriptions, skipping blank lines and '#' comments."""
    rooms = []
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            rooms.append(line)
    return rooms


class RateLimit:
    """Simple thread-safe minimum-interval throttle between request starts."""

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
    """Map the data-URL mime type to a file extension (jpg/png/webp)."""
    head = data_url[:32].lower()
    if "image/png" in head:
        return "png"
    if "image/webp" in head:
        return "webp"
    return "jpg"  # gemini-3-pro-image returns JPEG by default


def generate_one(session, api_key, model, system_prompt, user_prompt,
                 out_base, ratelimit, max_retries=4, timeout=240):
    """Generate image(s) for one prompt.

    Writes every image the model returns. The first is `<out_base>.<ext>`, any
    extras are `<out_base>_b.<ext>`, `<out_base>_c.<ext>`, etc. (Nano Banana Pro
    commonly returns 2 candidates per call.)

    Returns (ok, note, saved_files, cost_usd).
    """
    headers = {
        "Authorization": f"Bearer {api_key}",
        "Content-Type": "application/json",
        # Optional attribution headers accepted by OpenRouter.
        "HTTP-Referer": "https://github.com/ferrum-engine/rpg",
        "X-Title": "Ferrum Dungeon Dataset",
    }
    body = {
        "model": model,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_prompt},
        ],
        "modalities": ["image", "text"],
        "usage": {"include": True},  # ask OpenRouter to report real $ cost
    }

    backoff = 3.0
    for attempt in range(1, max_retries + 1):
        ratelimit.wait()
        try:
            resp = session.post(OPENROUTER_URL, headers=headers,
                                 json=body, timeout=timeout)
        except requests.RequestException as exc:
            note = f"network error: {exc}"
            if attempt < max_retries:
                time.sleep(backoff)
                backoff *= 2
                continue
            return False, note, [], 0.0

        if resp.status_code == 429 or resp.status_code >= 500:
            # Rate limited or transient server error -> back off and retry.
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

        if "error" in data and data["error"]:
            msg = str(data["error"])[:200]
            # Some errors (quota, moderation) are not worth retrying.
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
            # Model refused or returned only text; retry once, then give up.
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
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--prompts", required=True, help="Room description txt file")
    ap.add_argument("--system", required=True, help="System prompt txt file")
    ap.add_argument("--out", required=True, help="Output directory for PNGs")
    ap.add_argument("--view-set", default="arch", choices=sorted(VIEW_SETS.keys()),
                    help="Which named set of views to render per room")
    ap.add_argument("--repeats", type=int, default=5,
                    help="Renders per (room, view). Total images = rooms x views x repeats")
    ap.add_argument("--workers", type=int, default=6,
                    help="Concurrent request workers")
    ap.add_argument("--model", default=DEFAULT_MODEL, help="OpenRouter model id")
    ap.add_argument("--limit", type=int, default=0,
                    help="Stop after this many NEW images (0 = no limit)")
    ap.add_argument("--min-interval", type=float, default=0.15,
                    help="Minimum seconds between request starts (throttle)")
    ap.add_argument("--dry-run", action="store_true",
                    help="List the work to be done without calling the API")
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

    rooms = load_prompts(args.prompts)
    if not rooms:
        print("ERROR: no room prompts found", file=sys.stderr)
        return 2
    system_prompt = open(args.system, encoding="utf-8").read().strip()

    os.makedirs(args.out, exist_ok=True)
    meta_path = os.path.join(args.out, "metadata.jsonl")

    # Build the full task list: every room x every FIXED view x repeats.
    # Each task = one API call producing one image. Resume support: skip a task
    # if an image file already exists for its base name (any extension).
    views = VIEW_SETS[args.view_set]
    tasks = []
    skipped = 0
    for room in rooms:
        slug = slugify(room)
        for view_key, view_desc in views:
            user_prompt = build_view_prompt(room, view_desc)
            for rep in range(args.repeats):
                base_name = f"{slug}_{view_key}_{rep:02d}"
                out_base = os.path.join(args.out, base_name)
                existing = [p for p in glob.glob(out_base + "*")
                            if not p.endswith(".part")]
                if existing:
                    skipped += 1
                    continue
                tasks.append((room, view_key, user_prompt, base_name, out_base))

    total_calls = len(rooms) * len(views) * args.repeats
    if args.limit > 0:
        tasks = tasks[:args.limit]

    # gemini-3.1-flash-lite-image bills ~$0.034 per image (1 image per call).
    per_call_est = 0.034
    est_cost = len(tasks) * per_call_est
    print(f"Rooms:            {len(rooms)}")
    print(f"View set:         {args.view_set} "
          f"({len(views)}: {', '.join(k for k, _ in views)})")
    print(f"Repeats/view:     {args.repeats}")
    print(f"Target images:    {total_calls}")
    print(f"Already present:  {skipped}")
    print(f"To generate:      {len(tasks)} images")
    print(f"Model:            {args.model}")
    print(f"Workers:          {args.workers}")
    print(f"Est. cost:        ~${est_cost:,.2f} "
          f"({len(tasks)} images x ~${per_call_est:.3f})")
    print(f"Output dir:       {args.out}")

    if args.dry_run:
        for t in tasks[:8]:
            print(f"  would gen {t[3]}: {t[2][:100]}")
        if len(tasks) > 8:
            print(f"  ... and {len(tasks) - 8} more")
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
        room, view_key, user_prompt, base_name, out_base = task
        session = requests.Session()
        ok, note, saved, cost = generate_one(
            session, api_key, args.model, system_prompt,
            user_prompt, out_base, ratelimit)
        if ok:
            with meta_lock:
                with open(meta_path, "a", encoding="utf-8") as fh:
                    for fn in saved:
                        fh.write(json.dumps({
                            "file": fn,
                            "room": room,
                            "view": view_key,
                            "prompt": user_prompt,
                            "model": args.model,
                        }) + "\n")
        with counter_lock:
            counters["ok" if ok else "fail"] += 1
            counters["images"] += len(saved)
            counters["cost"] += cost
            done = counters["ok"] + counters["fail"]
            imgs = counters["images"]
            spent = counters["cost"]
        elapsed = time.monotonic() - start
        rate = done / elapsed if elapsed > 0 else 0
        eta = (len(tasks) - done) / rate if rate > 0 else 0
        status = "OK " if ok else "FAIL"
        print(f"[{done}/{len(tasks)}] {status} {base_name} ({note}) "
              f"| {imgs} imgs | ${spent:.2f} | ETA {eta/60:.1f}m", flush=True)
        return ok

    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = [pool.submit(worker, t) for t in tasks]
        for _ in as_completed(futures):
            pass

    elapsed = time.monotonic() - start
    print(f"\nDone. {counters['ok']} ok, {counters['fail']} failed "
          f"({counters['images']} images) in {elapsed/60:.1f} min. "
          f"Spent ~${counters['cost']:.2f}.")
    print(f"Metadata: {meta_path}")
    return 0 if counters["fail"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
