#!/bin/bash
# Khởi động toàn bộ hệ thống demo trên 1 máy.
# Usage: ./scripts/start-demo.sh
#
# Cần chạy với quyền root (hoặc sudo) vì inotify watcher cần quyền đọc /proc.
# Hoặc chạy với user thường nếu watch_dirs không cần quyền đặc biệt.

set -e
cd "$(dirname "$0")/.."

CENTRAL_BIN="./build/central-service/oob-central"
AGENT_BIN="./build/agent/oob-agent"
CENTRAL_CONFIG="./mock/central-config.json"
AGENT_CONFIG="./mock/agent-config.json"
LOG_DIR="/tmp/oob-demo-logs"

# ── Kiểm tra binaries và config ──────────────────────────────────────────────
if [ ! -f "$CENTRAL_BIN" ] || [ ! -f "$AGENT_BIN" ]; then
    echo "ERROR: Chưa build. Chạy trước:"
    echo "  cmake -B build && cmake --build build -j\$(nproc)"
    exit 1
fi

if [ ! -f "$CENTRAL_CONFIG" ]; then
    echo "ERROR: Không tìm thấy $CENTRAL_CONFIG"
    echo "  Tạo file từ template: cp mock/central-config.json.example mock/central-config.json"
    exit 1
fi

mkdir -p "$LOG_DIR"
mkdir -p /tmp/demo-prod-app/config /tmp/demo-prod-app/scripts
echo '{"version":"1.0.0","env":"prod"}' > /tmp/demo-prod-app/config/app.json
echo '[database]
host=db.internal
port=5432' > /tmp/demo-prod-app/config/db.conf

# ── Docker services ───────────────────────────────────────────────────────────
echo ">>> Khởi động Docker containers..."
docker start elasticsearch grafana jenkins 2>/dev/null || true

echo -n ">>> Chờ Elasticsearch..."
for i in $(seq 1 30); do
    if curl -s http://localhost:9200/_cluster/health &>/dev/null; then
        echo " OK"
        break
    fi
    echo -n "."
    sleep 2
done

# ── oob-central ───────────────────────────────────────────────────────────────
echo ">>> Khởi động oob-central..."
"$CENTRAL_BIN" --config "$CENTRAL_CONFIG" \
    > "$LOG_DIR/central.log" 2>&1 &
CENTRAL_PID=$!
echo "    PID=$CENTRAL_PID  log=$LOG_DIR/central.log"

# Chờ central bind port
sleep 2
if ! curl -s http://localhost:8080/health &>/dev/null; then
    echo "ERROR: Central không khởi động được. Xem log:"
    tail -20 "$LOG_DIR/central.log"
    exit 1
fi
echo "    Health check: OK"

# ── oob-agent ─────────────────────────────────────────────────────────────────
echo ">>> Khởi động oob-agent..."
"$AGENT_BIN" --config "$AGENT_CONFIG" \
    > "$LOG_DIR/agent.log" 2>&1 &
AGENT_PID=$!
echo "    PID=$AGENT_PID  log=$LOG_DIR/agent.log"
sleep 1

# ── Trạng thái ───────────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════════════════════"
echo "  OOB Deployment Monitor — DEMO READY"
echo "════════════════════════════════════════════════════════"
echo ""
echo "  Services:"
echo "    oob-central  : http://localhost:8080/health"
echo "    Elasticsearch: http://localhost:9200"
echo "    Grafana      : http://localhost:3000  (admin/admin)"
echo "    Jenkins      : http://localhost:8081  (admin/admin123)"
echo ""
echo "  Watching: /tmp/demo-prod-app/"
echo "  Audit log: /tmp/oob-audit.log"
echo ""
echo "  Logs:"
echo "    tail -f $LOG_DIR/central.log"
echo "    tail -f $LOG_DIR/agent.log"
echo "    tail -f /tmp/oob-audit.log"
echo ""
echo "  Demo commands:"
echo "    # AUTHORIZED deploy (mở window trước):"
echo "    curl -s -X POST http://localhost:8080/api/v1/deploy-window \\"
echo "      -H 'Content-Type: application/json' \\"
echo "      -d '{\"action\":\"OPEN\",\"project\":\"webapp\",\"server\":\"local-demo\",\"ttl_sec\":120}'"
echo ""
echo "    # UNAUTHORIZED drift (không mở window):"
echo "    echo 'hacked' > /tmp/demo-prod-app/config/app.json"
echo ""
echo "  Dừng demo: ./scripts/stop-demo.sh"
echo ""

# Lưu PIDs để stop-demo.sh dùng
echo "$CENTRAL_PID" > /tmp/oob-demo-central.pid
echo "$AGENT_PID"   > /tmp/oob-demo-agent.pid
