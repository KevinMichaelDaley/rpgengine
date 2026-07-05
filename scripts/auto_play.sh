#!/bin/bash
# Auto-play: cycles through DPO token files on chimera server + local client.
set -e

DELAY=120
DATASET_DIR="${1:-datasets/dpo_txt}"
SERVER_HOST="192.168.50.186"
BASE_PORT=40080
RPG_DIR="${RPG_DIR:-$HOME/rpg}"

echo "=== Auto-Play Procgen Levels ==="
echo "Host: $SERVER_HOST  |  Dataset: $DATASET_DIR  |  Delay: ${DELAY}s"
echo ""

COUNT=0
for txt in "$RPG_DIR/$DATASET_DIR"/*.txt; do
    [ -f "$txt" ] || continue
    COUNT=$((COUNT + 1))
    PORT=$((BASE_PORT + COUNT))
    BASENAME=$(basename "$txt" .txt)
    
    echo ""
    echo "=== Level $COUNT: $BASENAME (port $PORT) ==="
    
    # Kill previous client
    pkill -f "demo_client $SERVER_HOST" 2>/dev/null || true
    sleep 1
    
    # Start server on chimera
    ssh "$SERVER_HOST" "cd $RPG_DIR && timeout $((DELAY + 30)) ./build/demo_server $PORT 999 --phys-workers 2 --net-workers 1 2>&1 | grep -v 'SENSE:'" &
    SERVER_PID=$!
    sleep 2
    
    # Copy token to server for potential server-side collision
    scp "$txt" "$SERVER_HOST:/tmp/current_level.txt" 2>/dev/null || true
    
    # Start client locally with procgen mesh
    echo "  Client: $txt"
    "$RPG_DIR/build/demo_client" "$SERVER_HOST" "$PORT" --procgen "$txt" 2>&1 | grep -v "avg recv" &
    CLIENT_PID=$!
    
    sleep "$DELAY"
    
    kill $CLIENT_PID 2>/dev/null || true
    kill $SERVER_PID 2>/dev/null || true
    ssh "$SERVER_HOST" "pkill -f 'demo_server $PORT'" 2>/dev/null || true
    wait 2>/dev/null || true
    
done

echo ""
echo "Done. $COUNT levels displayed."
