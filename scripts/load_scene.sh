#!/bin/bash
# Send spawn commands to the editor TCP port.
# Usage: ./scripts/load_scene.sh [commands_file] [edit_port]
FILE="${1:-levels/cube_tower_10.txt}"
PORT="${2:-9100}"

nc -q 1 127.0.0.1 "$PORT" < "$FILE"
