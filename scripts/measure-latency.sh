#!/bin/bash
# NFR-01: Measure Shadow Deployment detection latency.
# Time from file change → audit log entry.
# Usage: ./scripts/measure-latency.sh [N]   (default N=5 runs)

set -e

WATCH_DIR=${WATCH_DIR:-/tmp/demo-prod-app}
AUDIT_LOG=${AUDIT_LOG:-/tmp/oob-audit.log}
N=${1:-5}

if [ ! -f "$AUDIT_LOG" ]; then
    echo "ERROR: Audit log not found at $AUDIT_LOG"
    echo "       Make sure oob-central is running."
    exit 1
fi

mkdir -p "$WATCH_DIR"
echo "=== NFR-01 Latency Measurement (${N} runs) ==="
echo "Watch dir : $WATCH_DIR"
echo "Audit log : $AUDIT_LOG"
echo "Limit     : 60 000 ms"
echo ""

latencies=()

for i in $(seq 1 "$N"); do
    TEST_FILE="$WATCH_DIR/latency-probe-$i-$$.txt"

    # T0: before the write
    t0=$(date +%s%3N)   # milliseconds

    echo "probe $i" > "$TEST_FILE"

    # Poll audit log for this file path
    found=0
    deadline=$((t0 + 60000))
    while true; do
        now=$(date +%s%3N)
        if grep -qF "$TEST_FILE" "$AUDIT_LOG" 2>/dev/null; then
            latency=$((now - t0))
            latencies+=("$latency")
            printf "Run %-2s: %d ms\n" "$i" "$latency"
            found=1
            break
        fi
        if [ "$now" -ge "$deadline" ]; then
            echo "Run $i : TIMEOUT (> 60 s)"
            latencies+=(-1)
            break
        fi
        sleep 0.05
    done

    rm -f "$TEST_FILE"
    sleep 1
done

echo ""
echo "=== Summary ==="
sum=0; count=0; max=-1; min=999999
for v in "${latencies[@]}"; do
    [ "$v" -lt 0 ] && continue
    sum=$((sum + v))
    count=$((count + 1))
    [ "$v" -gt "$max" ] && max=$v
    [ "$v" -lt "$min" ] && min=$v
done

if [ "$count" -gt 0 ]; then
    avg=$((sum / count))
    echo "Samples : $count / $N"
    echo "Min     : ${min} ms"
    echo "Avg     : ${avg} ms"
    echo "Max     : ${max} ms"
    echo "Limit   : 60 000 ms  (NFR-01)"
    echo ""
    if [ "$max" -lt 60000 ]; then
        echo "RESULT  : PASS ✓"
    else
        echo "RESULT  : FAIL ✗  (max exceeds 60 s limit)"
    fi
else
    echo "RESULT  : FAIL ✗  (all runs timed out)"
fi
