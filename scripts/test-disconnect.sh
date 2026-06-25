#!/bin/bash
# NFR-05: Verify no event loss during a 5-minute Central Service outage.
# The Agent must buffer events in memory and flush them on reconnect.
#
# Usage: ./scripts/test-disconnect.sh
# Both oob-agent and oob-central must be running.

set -e

WATCH_DIR=${WATCH_DIR:-/tmp/demo-prod-app}
AUDIT_LOG=${AUDIT_LOG:-/tmp/oob-audit.log}
PAUSE_SEC=${PAUSE_SEC:-300}   # 5 minutes; set to 30 for a quick smoke test

CENTRAL_PID=$(pgrep -x oob-central 2>/dev/null | head -1)
AGENT_PID=$(pgrep -x oob-agent    2>/dev/null | head -1)

[ -z "$CENTRAL_PID" ] && { echo "ERROR: oob-central not running"; exit 1; }
[ -z "$AGENT_PID"   ] && { echo "ERROR: oob-agent not running";   exit 1; }

mkdir -p "$WATCH_DIR"
before=$(wc -l < "$AUDIT_LOG" 2>/dev/null || echo 0)

echo "=== NFR-05: Event buffer test ==="
echo "Central PID : $CENTRAL_PID"
echo "Agent   PID : $AGENT_PID"
echo "Pause       : ${PAUSE_SEC} s"
echo "Audit log   : $AUDIT_LOG  (${before} existing lines)"
echo ""

echo "[1] Suspending oob-central at $(date '+%T')…"
kill -STOP "$CENTRAL_PID"

echo "[2] Creating 5 test files (one every $((PAUSE_SEC / 5)) s)…"
interval=$((PAUSE_SEC / 5))
test_files=()
for i in 1 2 3 4 5; do
    f="$WATCH_DIR/nfr05-disconnect-${i}-$$.txt"
    echo "test file $i — $(date)" > "$f"
    test_files+=("$f")
    echo "    Created $f"
    [ "$i" -lt 5 ] && sleep "$interval"
done

echo "[3] Resuming oob-central at $(date '+%T')…"
kill -CONT "$CENTRAL_PID"

echo "[4] Waiting 30 s for queue to flush…"
sleep 30

after=$(wc -l < "$AUDIT_LOG" 2>/dev/null || echo 0)
new=$((after - before))

echo ""
echo "=== Result ==="
echo "Events created : 5"
echo "Events in log  : $new"

# Cleanup test files
for f in "${test_files[@]}"; do rm -f "$f"; done

if [ "$new" -ge 5 ]; then
    echo "NFR-05 : PASS ✓  (no event loss during ${PAUSE_SEC} s outage)"
else
    echo "NFR-05 : FAIL ✗  (expected 5 events, got ${new})"
    exit 1
fi
