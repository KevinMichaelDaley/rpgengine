#!/usr/bin/env python3
"""Generate a dungeon ASCII grid from a text prompt via OpenRouter (Gemini)."""

import argparse
import json
import os
import sys
import urllib.request

SYSTEM_PROMPT = """\
You are a dungeon floor plan generator for a voxel game engine. You output ASCII grid layouts that get converted into 3D voxel dungeons.

## Output Format

Output ONLY the ASCII grid. No explanation, no markdown fences. Each floor is preceded by a header line like:
=== FLOOR 0: NAME ===

## Grid Characters

- W = wall (impassable solid)
- R = generic room
- B = boss room
- G = generic room (alternate)
- P = private/special room
- E = entrance room
- . = corridor connector (absorbed into adjacent room, creates doorway)
- ^ = stair going up (connects to v on the next floor at the SAME x column)
- v = stair going down (connects to ^ on the previous floor at the SAME x column)

## Rules

1. Characters are SPACE-SEPARATED (e.g., "W W R R W", not "WWRRW").
2. All floors must have the SAME width and height.
3. Rooms are contiguous regions of the same character. They get flood-filled.
4. Adjacent rooms MUST be connected by '.' corridor cells between them, otherwise there is no doorway. Rooms separated by W with no '.' between them are sealed off.
5. Stair pairs: ^ on floor N must align (same x column) with v on floor N+1. They get absorbed into adjacent rooms and create vertical connections. Place them adjacent to room cells, not isolated.
6. The outermost ring should be W (walls).
7. Make rooms interesting shapes — L-shaped, irregular, varying sizes. Not all rectangles.
8. Include dead ends, branching corridors, secret rooms (S).
9. Every room must be reachable from the entrance via . connectors and/or stairs.
10. Grids should be at least 15x15 per floor for interesting layouts.
11. Multi-floor dungeons: use 2-4 floors connected by stair pairs.

## Example (2-floor dungeon)

=== FLOOR 0: CRYPT ===
W W W W W W W W W W W W W W W
W E E E W R R R R R R R R R W
W E E E . R R R R R R R R R W
W E E E W R R R R R R R R R W
W W . W W W W W W W . W W W W
W R R R R W W W W W . W W W W
W R R R R . G G G G . ^ . W W
W R R R R W G G G G W W W W W
W R R R R W G G G G W W W W W
W W W W W W W W W W W W W W W
=== FLOOR 1: SANCTUM ===
W W W W W W W W W W W W W W W
W P P P P P P P P P W B B B W
W P P P P P P P P P W B B B W
W P P P P P P P P P . B B B W
W P P P P P P P P P W B B B W
W P P P P P P P P P W B B B W
W P P P P P P P P P . v . W W
W P P P P P P P P P W W W W W
W P P P P P P P P P W W W W W
W W W W W W W W W W W W W W W

Notice: ^ on floor 0 at column 11 aligns with v on floor 1 at column 11. Both are adjacent to room cells. Rooms connect via '.' cells.
"""

def generate(prompt, model="google/gemini-2.5-flash", api_key=None):
    if not api_key:
        key_path = os.path.expanduser("~/.ssh/OPENROUTER_API_KEY")
        with open(key_path) as f:
            api_key = f.read().strip()

    payload = json.dumps({
        "model": model,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.9,
        "max_tokens": 8192,
    }).encode()

    req = urllib.request.Request(
        "https://openrouter.ai/api/v1/chat/completions",
        data=payload,
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
    )

    with urllib.request.urlopen(req, timeout=60) as resp:
        body = json.loads(resp.read())

    text = body["choices"][0]["message"]["content"]
    # Strip markdown fences if present
    if text.startswith("```"):
        lines = text.split("\n")
        lines = [l for l in lines if not l.startswith("```")]
        text = "\n".join(lines)
    return text.strip()


def main():
    ap = argparse.ArgumentParser(description="Generate dungeon ASCII grid via LLM")
    ap.add_argument("prompt", help="Dungeon description prompt")
    ap.add_argument("-o", "--output", help="Output .asc file (default: stdout)")
    ap.add_argument("-m", "--model", default="google/gemini-2.5-flash",
                    help="OpenRouter model (default: gemini-2.5-flash)")
    args = ap.parse_args()

    text = generate(args.prompt, model=args.model)

    if args.output:
        with open(args.output, "w") as f:
            f.write(text + "\n")
        print(f"Wrote {args.output}", file=sys.stderr)
    else:
        print(text)


if __name__ == "__main__":
    main()
