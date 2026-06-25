#!/bin/bash
# NFR-02: Measure oob-agent CPU and RSS memory.
# Usage: ./scripts/measure-resources.sh [duration_sec]   (default 60 s)
# Requires: sysstat package  (sudo apt install sysstat)

DURATION=${1:-60}

PID=$(pgrep -x oob-agent 2>/dev/null | head -1)
if [ -z "$PID" ]; then
    echo "ERROR: oob-agent is not running (pgrep found nothing)."
    echo "       Start it first, then re-run this script."
    exit 1
fi

if ! command -v pidstat &>/dev/null; then
    echo "ERROR: pidstat not found.  Install with:  sudo apt install sysstat"
    exit 1
fi

echo "=== NFR-02 Resource Measurement ==="
echo "PID      : $PID"
echo "Duration : ${DURATION} s"
echo "Limits   : CPU < 2 %,  RSS < 51 200 KB (50 MB)"
echo ""

TMP=$(mktemp /tmp/pidstat-oob-XXXXXX.txt)
pidstat -u -r -p "$PID" 1 "$DURATION" | tee "$TMP"

echo ""
echo "=== Summary ==="

# CPU: 7th column in pidstat -u output (% usr + sys ≈ %CPU)
cpu_max=$(grep -E "^[0-9]" "$TMP" | awk '{print $7}' | sort -n | tail -1)
# RSS: 6th column in pidstat -r output (KB)
rss_max=$(grep -E "^[0-9]" "$TMP" | awk '{print $6}' | sort -n | tail -1)

echo "CPU peak : ${cpu_max} %   (limit: 2 %)"
echo "RSS peak : ${rss_max} KB  (limit: 51 200 KB)"
echo ""

cpu_pass=$(awk "BEGIN { print (${cpu_max:-99} < 2.0) ? 1 : 0 }")
rss_pass=$([ "${rss_max:-99999}" -lt 51200 ] && echo 1 || echo 0)

[ "$cpu_pass" = "1" ] && echo "CPU : PASS ✓" || echo "CPU : FAIL ✗"
[ "$rss_pass" = "1" ] && echo "RSS : PASS ✓" || echo "RSS : FAIL ✗"

rm -f "$TMP"
