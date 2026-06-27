#!/bin/bash
# Install oob-agent on a production server.
# Usage: sudo ./deploy/install-agent.sh
#
# Chạy script này trên MỖI production server cần giám sát.
# Central service chạy ở máy riêng (xem install-central.sh).

set -e

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_DIR/build"
DEPLOY_DIR="$REPO_DIR/deploy"

echo "=== Installing oob-agent ==="

# Binary
install -m 755 "$BUILD_DIR/agent/oob-agent" /usr/local/bin/oob-agent
echo "Binary  : /usr/local/bin/oob-agent"

# Default config (chỉ tạo nếu chưa có — không ghi đè config hiện tại)
mkdir -p /etc/oob-agent
if [ ! -f /etc/oob-agent/config.json ]; then
    install -m 644 "$REPO_DIR/mock/agent-config.json" /etc/oob-agent/config.json
    echo "Config  : /etc/oob-agent/config.json  ← hãy chỉnh central_url, watch_dirs, server"
else
    echo "Config  : /etc/oob-agent/config.json  (đã tồn tại, giữ nguyên)"
fi

# Systemd unit
install -m 644 "$DEPLOY_DIR/oob-agent.service" /etc/systemd/system/oob-agent.service
echo "Service : /etc/systemd/system/oob-agent.service"

systemctl daemon-reload
systemctl enable oob-agent

echo ""
echo "=== Xong. Bước tiếp theo ==="
echo "  1. nano /etc/oob-agent/config.json"
echo "     - central_url: địa chỉ máy chạy oob-central"
echo "     - server     : tên server này (ví dụ 'prod-web-01')"
echo "     - watch_dirs : thư mục cần giám sát"
echo "  2. systemctl start oob-agent"
echo "  3. systemctl status oob-agent"
