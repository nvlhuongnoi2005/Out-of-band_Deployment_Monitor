#!/bin/bash
# NFR-02 / stress test: run concurrent file writes and sample oob-agent usage.
# Usage:
#   ./scripts/stress-file-writes.sh
#   THREADS=10 MILESTONES="100 200 500" WATCH_DIR=/tmp/demo-prod-app ./scripts/stress-file-writes.sh

set -euo pipefail
export LC_ALL=C

WATCH_DIR=${WATCH_DIR:-/tmp/demo-prod-app}
AUDIT_LOG=${AUDIT_LOG:-/tmp/oob-audit.log}
THREADS=${THREADS:-10}
MILESTONES=${MILESTONES:-"100 200 500"}
SAMPLE_INTERVAL=${SAMPLE_INTERVAL:-0.2}
AUDIT_TIMEOUT_SEC=${AUDIT_TIMEOUT_SEC:-60}
CPU_LIMIT=${CPU_LIMIT:-2.0}
RSS_LIMIT_KB=${RSS_LIMIT_KB:-51200}
AGENT_PID=${AGENT_PID:-$(pgrep -x oob-agent 2>/dev/null | head -1 || true)}

if [ -z "$AGENT_PID" ]; then
    echo "ERROR: oob-agent is not running (pgrep found nothing)."
    echo "       Start Agent first, or set AGENT_PID=<pid>."
    exit 1
fi

if ! kill -0 "$AGENT_PID" 2>/dev/null; then
    echo "ERROR: AGENT_PID=$AGENT_PID is not a running process."
    exit 1
fi

if [ ! -f "$AUDIT_LOG" ]; then
    echo "ERROR: Audit log not found at $AUDIT_LOG"
    echo "       Start oob-central first, or set AUDIT_LOG=<path>."
    exit 1
fi

mkdir -p "$WATCH_DIR"

echo "=== Concurrent File Write Stress Test ==="
echo "Agent PID       : $AGENT_PID"
echo "Watch dir       : $WATCH_DIR"
echo "Audit log       : $AUDIT_LOG"
echo "Threads         : $THREADS"
echo "Milestones      : $MILESTONES"
echo "Resource limits : CPU < ${CPU_LIMIT} %, RSS < ${RSS_LIMIT_KB} KB"
echo ""

sample_resources() {
    local output=$1
    while kill -0 "$AGENT_PID" 2>/dev/null; do
        LC_ALL=C ps -p "$AGENT_PID" -o %cpu=,rss= 2>/dev/null | LC_ALL=C awk -v ts="$(date +%s%3N)" '
            NF >= 2 { print ts, $1, $2; fflush(); }
        ' >> "$output"
        sleep "$SAMPLE_INTERVAL"
    done
}

summarize_resources() {
    local sample_file=$1

    if [ ! -s "$sample_file" ]; then
        echo "CPU peak : n/a"
        echo "RSS peak : n/a"
        echo "CPU/RSS  : WARN (no samples)"
        return
    fi

    local cpu_peak
    local rss_peak
    cpu_peak=$(LC_ALL=C awk 'BEGIN{max=0} {if ($2+0 > max) max=$2+0} END{printf "%.2f", max}' "$sample_file")
    rss_peak=$(LC_ALL=C awk 'BEGIN{max=0} {if ($3+0 > max) max=$3+0} END{printf "%d", max}' "$sample_file")

    echo "CPU peak : ${cpu_peak} %   (limit: ${CPU_LIMIT} %)"
    echo "RSS peak : ${rss_peak} KB  (limit: ${RSS_LIMIT_KB} KB)"

    local cpu_pass
    local rss_pass
    cpu_pass=$(LC_ALL=C awk "BEGIN { print ($cpu_peak < $CPU_LIMIT) ? 1 : 0 }")
    if [ "$rss_peak" -lt "$RSS_LIMIT_KB" ]; then
        rss_pass=1
    else
        rss_pass=0
    fi

    [ "$cpu_pass" = "1" ] && echo "CPU      : PASS" || echo "CPU      : FAIL"
    [ "$rss_pass" = "1" ] && echo "RSS      : PASS" || echo "RSS      : FAIL"
}

wait_for_audit_count() {
    local run_id=$1
    local expected=$2
    local deadline=$(( $(date +%s) + AUDIT_TIMEOUT_SEC ))
    local count=0

    while true; do
        count=$(grep -F "$run_id" "$AUDIT_LOG" 2>/dev/null | wc -l)
        if [ "$count" -ge "$expected" ]; then
            echo "$count"
            return 0
        fi
        if [ "$(date +%s)" -ge "$deadline" ]; then
            echo "$count"
            return 1
        fi
        sleep 0.2
    done
}

run_milestone() {
    local total=$1
    local run_id="stress-${total}-$(date +%s)-$$"
    local sample_file
    sample_file=$(mktemp /tmp/oob-stress-samples-XXXXXX.txt)
    local base=$((total / THREADS))
    local remainder=$((total % THREADS))

    echo "--- Milestone: ${total} files, ${THREADS} threads ---"
    echo "Run id  : $run_id"

    sample_resources "$sample_file" &
    local sampler_pid=$!

    local t0
    t0=$(date +%s%3N)

    local -a worker_pids=()
    for worker in $(seq 1 "$THREADS"); do
        (
            count=$base
            if [ "$worker" -le "$remainder" ]; then
                count=$((count + 1))
            fi

            for i in $(seq 1 "$count"); do
                file="$WATCH_DIR/${run_id}-w${worker}-f${i}.txt"
                printf 'run=%s worker=%s file=%s ts=%s\n' \
                    "$run_id" "$worker" "$i" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$file"
            done
        ) &
        worker_pids+=("$!")
    done

    for pid in "${worker_pids[@]}"; do
        wait "$pid"
    done

    local t1
    t1=$(date +%s%3N)
    local elapsed_ms=$((t1 - t0))

    sleep 1
    kill "$sampler_pid" 2>/dev/null || true
    wait "$sampler_pid" 2>/dev/null || true

    local audit_count
    if audit_count=$(wait_for_audit_count "$run_id" "$total"); then
        audit_result="PASS"
    else
        audit_result="WARN"
    fi

    local throughput
    throughput=$(LC_ALL=C awk "BEGIN { if ($elapsed_ms > 0) printf \"%.2f\", ($total * 1000) / $elapsed_ms; else print \"inf\" }")

    echo "Elapsed  : ${elapsed_ms} ms"
    echo "Throughput: ${throughput} files/s"
    echo "Audit    : ${audit_count}/${total} entries (${audit_result})"
    summarize_resources "$sample_file"
    rm -f "$sample_file"
    echo ""
}

for milestone in $MILESTONES; do
    run_milestone "$milestone"
done

echo "=== Done ==="
