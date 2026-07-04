#!/bin/bash
# Launch demo_server with each generated DPO level sequentially.
# Usage: ./scripts/loop_levels.sh [--port BASE_PORT] [--dir DATASET_DIR]
# Press Enter to advance to the next level.

BASE_PORT=${BASE_PORT:-40080}
DATASET_DIR="${1:-datasets/dpo_pairs}"
SERVER_BIN="${SERVER_BIN:-./build/demo_server}"

echo "=== Level Viewer Loop ==="
echo "Dataset: $DATASET_DIR"
echo "Port:    $BASE_PORT"
echo ""

COUNT=0
for json in "$DATASET_DIR"/*.json; do
    [ -f "$json" ] || continue
    COUNT=$((COUNT + 1))
    PORT=$((BASE_PORT + COUNT))
    BASENAME=$(basename "$json" .json)
    
    echo ""
    echo "════════════════════════════════════════════════"
    echo "  Level $COUNT: $BASENAME"
    echo "  Port: $PORT  |  File: $json"
    echo "════════════════════════════════════════════════"
    
    echo "Starting server on port $PORT..."
    $SERVER_BIN $PORT 0 --scene "$json" --phys-workers 4 --net-workers 1 &
    SERVER_PID=$!
    sleep 2
    
    echo ""
    echo "Server PID: $SERVER_PID"
    echo "Press Enter to kill and advance to next level (or 'q' to quit)..."
    read -r REPLY
    
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
    
    if [ "$REPLY" = "q" ]; then
        echo "Quit."
        exit 0
    fi
done

echo ""
echo "Done. Displayed $COUNT levels."
