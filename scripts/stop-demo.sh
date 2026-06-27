#!/bin/bash
# Dừng toàn bộ demo.
# Usage: ./scripts/stop-demo.sh

echo ">>> Dừng oob-agent và oob-central..."

for PIDFILE in /tmp/oob-demo-central.pid /tmp/oob-demo-agent.pid; do
    if [ -f "$PIDFILE" ]; then
        PID=$(cat "$PIDFILE")
        if kill -0 "$PID" 2>/dev/null; then
            kill "$PID"
            echo "    Stopped PID $PID"
        fi
        rm -f "$PIDFILE"
    fi
done

# Fallback: pkill theo tên
pkill -f "oob-central" 2>/dev/null || true
pkill -f "oob-agent"   2>/dev/null || true

echo ">>> Dừng Docker containers..."
docker stop elasticsearch grafana jenkins 2>/dev/null || true

echo ">>> Cleanup..."
rm -rf /tmp/demo-prod-app /tmp/oob-audit.log /tmp/oob-remediation.log /tmp/oob-demo-logs

echo "Done."
