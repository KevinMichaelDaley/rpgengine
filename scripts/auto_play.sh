#!/bin/bash
# Auto-play: cycles through DPO levels on chimera server + local client.
# Usage: ./scripts/auto_play.sh [--delay SEC] [--dir DIR] [--server-host HOST]

DELAY="${DELAY:-5}"
DATASET_DIR="${1:-datasets/dpo_pairs}"
SERVER_HOST="192.168.50.186"
BASE_PORT=40080
RPG_DIR="${RPG_DIR:-$HOME/rpg}"

echo "=== Auto-Play Level Viewer ==="
echo "Host: $SERVER_HOST  |  Dataset: $DATASET_DIR  |  Delay: ${DELAY}s"
echo ""

COUNT=0
for json in "$RPG_DIR/$DATASET_DIR"/*.json; do
    [ -f "$json" ] || continue
    COUNT=$((COUNT + 1))
    PORT=$((BASE_PORT + COUNT))
    BASENAME=$(basename "$json" .json)
    SERVER_JSON="$RPG_DIR/$DATASET_DIR/$BASENAME.json"
    
    echo ""
    echo "=== Level $COUNT: $BASENAME (port $PORT) ==="
    
    # Kill any previous client
    pkill -f "demo_client $SERVER_HOST" 2>/dev/null || true
    sleep 0.5
    
    # Start server on chimera via SSH, background, with timeout
    ssh "$SERVER_HOST" "cd $RPG_DIR && timeout $((DELAY + 10)) ./build/demo_server $PORT 0 --scene '$SERVER_JSON' --phys-workers 2 --net-workers 1 2>&1 | grep -v 'SENSE:'" &
    SERVER_SSH_PID=$!
    sleep 2
    
    # Start local client
    echo "  Launching client..."
    timeout 65 "$RPG_DIR/build/demo_client" "$SERVER_HOST" "$PORT" 2>/dev/null &
    CLIENT_PID=$!
    
    # Wait for delay
    sleep "$DELAY"
    
    # Clean up
    kill $CLIENT_PID 2>/dev/null
    kill $SERVER_SSH_PID 2>/dev/null
    ssh "$SERVER_HOST" "pkill -f 'demo_server $PORT'" 2>/dev/null || true
    wait 2>/dev/null
    
done

echo ""
echo "Done. $COUNT levels displayed."
