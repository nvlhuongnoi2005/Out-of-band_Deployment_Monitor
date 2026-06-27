# Demo Guide — Out-of-Band Deployment Monitor

## Tổng quan demo

Demo trên **1 máy** (đơn giản, đủ để thuyết trình):

```
┌─────────────────────────────────────────────────────┐
│  Máy demo (Linux)                                   │
│                                                     │
│  [oob-agent]          theo dõi /tmp/demo-prod-app   │
│       │                                             │
│       │ HTTP :8080                                  │
│       ▼                                             │
│  [oob-central]        phân loại sự kiện             │
│       │                                             │
│       ├──► [Elasticsearch :9200]  lưu audit log     │
│       ├──► [Grafana :3000]        dashboard         │
│       ├──► [Jenkins :8081]        kiểm tra pipeline │
│       └──► Email alert                              │
└─────────────────────────────────────────────────────┘
```

---

## Bước 0 — Chuẩn bị (chỉ làm 1 lần)

```bash
# Tạo thư mục "production app" để giám sát
mkdir -p /tmp/demo-prod-app/config /tmp/demo-prod-app/scripts

# Build binaries (nếu chưa build)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

---

## Bước 1 — Khởi động hạ tầng

### Terminal 1 — Khởi động Docker services

```bash
docker start elasticsearch grafana jenkins

# Chờ Elasticsearch sẵn sàng (10-15 giây)
until curl -s http://localhost:9200/_cluster/health | grep -q '"status"'; do
    echo "Waiting for ES..."; sleep 2
done
echo "Elasticsearch ready"
```

### Terminal 2 — Khởi động oob-central

```bash
cd /home/lap/Desktop/tracking-deployment

# Tất cả settings đã có trong mock/central-config.json
./build/central-service/oob-central --config mock/central-config.json
```

> Config file: [mock/central-config.json](../mock/central-config.json)
> Template:    [mock/central-config.json.example](../mock/central-config.json.example)
>
> CLI flags vẫn hoạt động và **override** giá trị trong file nếu cần:
> ```bash
> # Ví dụ: dùng config nhưng đổi port
> ./build/central-service/oob-central --config mock/central-config.json --port 9090
> ```

**Kết quả mong đợi:**
```
=== Out-of-Band Deployment Monitor — Central Service v0.1.0 ===
[Central] HTTP server on port 8080
[Central] Audit log    : /tmp/oob-audit.log
[Central] Elasticsearch: enabled
[Central] Email alerts : enabled
[Central] Auto-remediation: enabled (Jenkins re-trigger)
```

### Terminal 3 — Khởi động oob-agent

```bash
cd /home/lap/Desktop/tracking-deployment

./build/agent/oob-agent --config mock/agent-config.json
```

**Kết quả mong đợi:**
```
=== Out-of-Band Deployment Monitor — Agent v0.1.0 ===
[Agent] Backend: inotify
[Watcher] Watching 1 directories via inotify
[Agent] Heartbeat interval: 30 s
```

### Kiểm tra health check

```bash
curl -s http://localhost:8080/health | python3 -m json.tool
# → {"status":"ok","service":"oob-central"}
```

---

## Demo Scenario 1 — Deploy hợp lệ (AUTHORIZED_CHANGE)

**Mô tả:** Jenkins chạy pipeline → Ansible thay đổi file → hệ thống nhận ra đây là deploy có phép.

### Terminal 4 — Theo dõi audit log realtime

```bash
tail -f /tmp/oob-audit.log | python3 -m json.tool --no-ensure-ascii
```

### Thực hiện deploy hợp lệ:

**Cách 1 — Dùng Jenkins pipeline (thực tế nhất):**
```
1. Mở browser: http://localhost:8081
2. Đăng nhập: admin / admin123
3. Vào job "webapp-deploy" → Build Now
4. Xem console output
```

**Cách 2 — Giả lập thủ công (nhanh hơn khi demo):**
```bash
# Mở deploy window (giả lập Jenkins mở trước khi deploy)
curl -s -X POST http://localhost:8080/api/v1/deploy-window \
  -H "Content-Type: application/json" \
  -d '{
    "action": "OPEN",
    "project": "webapp",
    "server": "local-demo",
    "ttl_sec": 120
  }' | python3 -m json.tool

# Giả lập Ansible thay đổi file
echo '{"version":"2.1.0","deployed_by":"jenkins","env":"prod"}' \
  > /tmp/demo-prod-app/config/app.json

echo '[database]
host=db.internal
port=5432
pool=10' > /tmp/demo-prod-app/config/db.conf

# Đóng deploy window
curl -s -X POST http://localhost:8080/api/v1/deploy-window \
  -H "Content-Type: application/json" \
  -d '{"action":"CLOSE","project":"webapp","server":"local-demo"}' \
  | python3 -m json.tool
```

**Kết quả mong đợi trong audit log:**
```json
{"classification": "AUTHORIZED_CHANGE", "event_type": "CREATE", "path": "/tmp/demo-prod-app/config/app.json", ...}
```

**Central terminal log:**
```
[Central] AUTHORIZED_CHANGE | server=local-demo | path=/tmp/demo-prod-app/config/app.json
```

---

## Demo Scenario 2 — Shadow Deployment (UNAUTHORIZED_DRIFT) ⚠️

**Mô tả:** Ai đó SSH vào server và sửa file ngoài luồng CI/CD — hệ thống phát hiện ngay lập tức.

> **Không mở deploy window trước bước này.**

```bash
# Hacker / engineer SSH vào server và sửa file config
echo '{"version":"HACKED","admin_bypass":true}' \
  > /tmp/demo-prod-app/config/app.json
```

**Kết quả trong vòng < 5 giây:**

1. **Central terminal:**
```
[Central] *** SHADOW DEPLOYMENT DETECTED ***
          | server=local-demo | path=/tmp/demo-prod-app/config/app.json
          | user=lap | proc=bash
```

2. **Audit log:**
```json
{
  "classification": "UNAUTHORIZED_DRIFT",
  "event_type": "MODIFY",
  "path": "/tmp/demo-prod-app/config/app.json",
  "username": "lap",
  "process_name": "bash",
  "detected_at": "2026-06-27T..."
}
```

3. **Email** tới địa chỉ admin đã cấu hình trong `mock/central-config.json`

4. **Auto-remediation** (nếu Jenkins online):
```
[Remediation] UNAUTHORIZED_DRIFT on local-demo — triggering Jenkins re-deploy for project webapp
[Remediation] Jenkins re-deploy queued for project webapp
```

---

## Demo Scenario 3 — Xóa file trái phép

```bash
rm /tmp/demo-prod-app/config/db.conf
```

**Kết quả:**
```json
{"classification": "UNAUTHORIZED_DRIFT", "event_type": "DELETE", "path": "...db.conf", ...}
```

---

## Demo Scenario 4 — Thay đổi permission

```bash
chmod 777 /tmp/demo-prod-app/config/app.json
```

**Kết quả:**
```json
{"classification": "UNAUTHORIZED_DRIFT", "event_type": "ATTRIB", ...}
```

---

## Xem kết quả trên Grafana

```
http://localhost:3000
Login: admin / admin
Dashboard: "OOB Deployment Monitor" (uid: algpt6)
```

**Các panel hiển thị:**
- Total Events (số sự kiện)
- Unauthorized vs Authorized ratio
- Events by Server
- Recent Events table (realtime)
- Classification Timeline

---

## Demo Scenario 5 — Agent disconnect rồi reconnect

**Mô tả:** Mạng bị mất → agent buffer events → reconnect → gửi hết.

```bash
# Suspend central (giả lập mất kết nối)
kill -STOP $(pgrep oob-central)

# Tạo 3 sự kiện khi central đang offline
echo "file1" > /tmp/demo-prod-app/config/file1.txt
echo "file2" > /tmp/demo-prod-app/config/file2.txt
echo "file3" > /tmp/demo-prod-app/config/file3.txt

echo "Central đang offline — agent đang buffer..."
sleep 5

# Resume central
kill -CONT $(pgrep oob-central)

echo "Central online lại — agent sẽ flush buffer trong vài giây"
```

**Kết quả:** Cả 3 events xuất hiện trong audit log sau khi central online.

---

## Cleanup sau demo

```bash
# Xóa demo files
rm -rf /tmp/demo-prod-app
rm -f /tmp/oob-audit.log /tmp/oob-remediation.log

# Stop Docker
docker stop elasticsearch grafana jenkins
```

---

## Câu hỏi thường gặp khi thuyết trình

**Q: Tại sao dùng inotify chứ không phải polling?**
> inotify là kernel event — agent 0% CPU khi không có thay đổi. Polling phải scan liên tục.

**Q: Nếu agent mất kết nối thì sao?**
> EventReporter có internal queue + retry timer 5s. Events không bị mất.

**Q: Auto-remediation có đảm bảo không bị trigger lặp lại không?**
> JenkinsRemediator có cooldown 300s per project — cùng 1 project chỉ trigger 1 lần trong 5 phút dù có 100 events.

**Q: Hệ thống có ảnh hưởng đến performance production không?**
> Agent chỉ tốn ~1% CPU và <50MB RAM (đo bằng `scripts/measure-resources.sh`).
