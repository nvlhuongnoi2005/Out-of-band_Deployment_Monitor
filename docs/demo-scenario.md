# Kịch Bản Demo Hệ Thống

Tài liệu này dùng để demo Out-of-Band Deployment Monitor theo mô hình Host + VM.
Mục tiêu là chứng minh hệ thống phát hiện thay đổi ngoài luồng CI/CD, phân loại
đúng thay đổi hợp lệ/trái phép, ghi audit log, cập nhật observability, trigger
Jenkins/Ansible remediation và chịu được workload ghi file đồng thời ở mức mini.

## 1. Mục Tiêu Demo

Sau buổi demo, người xem cần thấy được:

- Agent trên VM đang online và gửi heartbeat về Central.
- Thay đổi file thủ công ngoài Jenkins bị phân loại là `UNAUTHORIZED_DRIFT`.
- Thay đổi trong Deploy Window được phân loại là `AUTHORIZED_CHANGE`.
- Central ghi audit log dạng JSON Lines.
- Elasticsearch/Grafana đọc được event nếu bật observability.
- Jenkins remediation được trigger khi bật `jenkins.remediate=true`.
- Agent tiêu thụ tài nguyên thấp khi 10 worker ghi file đồng thời.

## 2. Mô Hình Demo

```text
Host
  - oob-central
  - Jenkins
  - Elasticsearch
  - Grafana
  - /tmp/oob-audit.log

Linux VM
  - oob-agent
  - watch dir: /tmp/demo-prod-app
```

Lưu ý quan trọng: trong config Agent trên VM, `central_url` phải trỏ về IP của
Host, ví dụ:

```json
"central_url": "http://HOST_IP:8080"
```

Không dùng `localhost` hoặc `127.0.0.1` trên VM, vì hai địa chỉ đó trỏ về chính
VM chứ không phải Host.

## 3. Chuẩn Bị Trước Khi Demo

### 3.1 Kiểm tra Central config

Trên Host, file `mock/central-config.json` cần bật các phần tích hợp nếu muốn
demo đầy đủ:

```json
{
  "port": 8080,
  "audit_log": "/tmp/oob-audit.log",
  "elasticsearch": {
    "host": "localhost",
    "port": 9200,
    "index": "oob-audit",
    "user": "",
    "pass": "",
    "https": false
  },
  "jenkins": {
    "url": "http://localhost:8081",
    "user": "admin",
    "token": "YOUR_JENKINS_API_TOKEN",
    "remediation_vm_ip": "VM_IP",
    "remediation_vm_user": "lap",
    "remediation_server": "prod-server-01",
    "ssl_verify": true,
    "remediate": true,
    "fail_open": false
  }
}
```

Nếu chỉ demo core detection/classification thì Elasticsearch, Grafana, SMTP và
Jenkins remediation có thể tắt. Khi đó chỉ cần audit log.

### 3.2 Chạy Central trên Host

```bash
./build/central-service/oob-central --config mock/central-config.json
```

Khi start đúng, log nên có:

```text
[Central] HTTP server on port 8080
[Central] Audit log    : "/tmp/oob-audit.log"
[Central] Elasticsearch: enabled
[Central] Auto-remediation: enabled (Jenkins re-trigger)
```

Nếu thấy `Elasticsearch: disabled` hoặc `Auto-remediation: disabled`, kiểm tra
lại `mock/central-config.json` và cách chạy Central.

Mở thêm terminal Host để theo dõi audit log:

```bash
tail -f /tmp/oob-audit.log
```

### 3.3 Chạy Agent trên VM

Trên VM:

```bash
sudo mkdir -p /tmp/demo-prod-app/config
sudo systemctl restart oob-agent
sudo systemctl status oob-agent
```

Theo dõi log Agent:

```bash
sudo journalctl -u oob-agent -f
```

Nếu chạy đúng, Central sẽ nhận heartbeat:

```text
[Central] HEARTBEAT "| agent=agent-01" "| server=prod-server-01" "| status=healthy"
```

## 4. Demo 1 - Heartbeat Agent

Mục tiêu: chứng minh VM Agent đang online và Central nhận được tín hiệu định kỳ.

Trên Host, quan sát log Central:

```text
[Central] HEARTBEAT "| agent=agent-01" "| server=prod-server-01" "| status=healthy"
```

Kết quả demo cần thấy:

- Central log có heartbeat từ `agent-01`.
- Trường `server` là `prod-server-01`.
- Trường `status` là `healthy`.

## 5. Demo 2 - Phát Hiện Unauthorized Drift

Mục tiêu: chứng minh thay đổi thủ công ngoài CI/CD bị phát hiện là shadow
deployment.

Trên VM:

```bash
echo "hacked" > /tmp/demo-prod-app/config/app.config
```

Trên Host/Central, log kỳ vọng:

```text
[Central] *** SHADOW DEPLOYMENT DETECTED *** "| server=prod-server-01" "| path=/tmp/demo-prod-app/config/app.config" "| user=lap" "| proc=bash"
```

Trong audit log kỳ vọng có:

```json
"classification":"UNAUTHORIZED_DRIFT"
```

Kết quả demo cần thấy:

- Central log có dòng `SHADOW DEPLOYMENT DETECTED`.
- Audit log có `classification:"UNAUTHORIZED_DRIFT"`.
- Audit log có path `/tmp/demo-prod-app/config/app.config`.
- Audit log có user/process thực hiện thay đổi, ví dụ `user=lap`, `proc=bash`.

## 6. Demo 3 - Jenkins Auto-Remediation

Mục tiêu: chứng minh Central có thể gọi Jenkins để restore file khi drift xảy ra.

Điều kiện:

- `jenkins.remediate=true` trong `mock/central-config.json`.
- Jenkins job name khớp với `project`, ví dụ `webapp`.
- Jenkins job có parameter:
  - `VM_IP`
  - `VM_USER`
  - `OOB_SERVER_NAME`
- Jenkins credential SSH có ID `vm-deploy-key`.
- Jenkins workspace có file `ansible/deploy-webapp.yml`.

Sau khi tạo unauthorized drift ở Demo 2, Central nên có log:

```text
[Remediation] UNAUTHORIZED_DRIFT on "prod-server-01" ...
[Remediation] Jenkins re-deploy queued for project "webapp"
```

Mở Jenkins job `webapp` để cho thấy build mới được queue/chạy.

Kết quả demo cần thấy:

- Central log có dòng `[Remediation] UNAUTHORIZED_DRIFT ...`.
- Central log có dòng `Jenkins re-deploy queued`.
- Jenkins job `webapp` có build mới.
- Pipeline Jenkins mở Deploy Window, chạy Ansible và restore file demo.
- File trong `/tmp/demo-prod-app` được khôi phục về trạng thái chuẩn.

Nếu không thấy Jenkins được gọi, kiểm tra:

```bash
grep -n '"remediate"' mock/central-config.json
grep -n '"url"' mock/central-config.json
grep -n '"token"' mock/central-config.json
```

## 7. Demo 4 - Authorized Change Bằng Deploy Window

Mục tiêu: chứng minh cùng là thay đổi file, nhưng nếu nằm trong Deploy Window thì
không bị cảnh báo nhầm.

Trên Host, mở Deploy Window:

```bash
curl -s -X POST http://localhost:8080/api/v1/deploy-window \
  -H "Content-Type: application/json" \
  -d '{"action":"OPEN","project":"webapp","server":"prod-server-01","ttl_sec":120}'
```

Trên VM, sửa file:

```bash
echo "deploy-ok" > /tmp/demo-prod-app/config/app.config
```

Trong audit log kỳ vọng có:

```json
"classification":"AUTHORIZED_CHANGE"
```

Đóng Deploy Window:

```bash
curl -s -X POST http://localhost:8080/api/v1/deploy-window \
  -H "Content-Type: application/json" \
  -d '{"action":"CLOSE","project":"webapp","server":"prod-server-01"}'
```

Kết quả demo cần thấy:

- API mở Deploy Window trả `status:"ok"`.
- Audit log có `classification:"AUTHORIZED_CHANGE"`.
- Không xuất hiện log `SHADOW DEPLOYMENT DETECTED` cho thay đổi trong window.
- API đóng Deploy Window trả `status:"ok"`.

## 8. Demo 5 - Elasticsearch Và Grafana

Mục tiêu: chứng minh audit event có thể được đưa vào observability pipeline.

Kiểm tra Elasticsearch trên Host:

```bash
curl http://localhost:9200/oob-audit/_count
curl "http://localhost:9200/oob-audit/_search?pretty&size=3"
```

Grafana datasource:

```text
URL: http://elasticsearch:9200
Index: oob-audit
Time field: @timestamp
```

Khi mở Grafana:

- Chọn time range `Last 15 minutes` hoặc `Last 1 hour`.
- Refresh dashboard.
- Kiểm tra panel/table có event mới.

Kết quả demo cần thấy:

- Elasticsearch index `oob-audit` có document mới.
- Document có `@timestamp`, `server`, `project`, `path`, `classification`.
- Grafana datasource kết nối được Elasticsearch.
- Dashboard/table hiển thị event mới trong time range đang chọn.

Nếu audit log có event nhưng Grafana chưa cập nhật:

- Kiểm tra Central có log `Elasticsearch: enabled`.
- Kiểm tra `curl http://localhost:9200/oob-audit/_count`.
- Kiểm tra datasource URL trong Grafana.
- Kiểm tra time range của dashboard.

## 9. Demo 6 - Performance Test Mini

Mục tiêu: chứng minh Agent không chiếm nhiều tài nguyên khi nhiều process ghi
file đồng thời.

Trên VM, copy script stress test nếu chưa có:

```bash
scp scripts/stress-file-writes.sh USER@VM_IP:/tmp/stress-file-writes.sh
```

Trên VM:

```bash
chmod +x /tmp/stress-file-writes.sh
sudo env THREADS=10 MILESTONES="100 200 500" \
  WATCH_DIR=/tmp/demo-prod-app AUDIT_LOG=/tmp/oob-audit.log \
  AUDIT_TIMEOUT_SEC=3 \
  /tmp/stress-file-writes.sh
```

Kết quả đo mẫu:

| Workload | Elapsed | Throughput | CPU peak Agent | RSS peak Agent | Audit trên Central |
|---|---:|---:|---:|---:|---:|
| 100 file / 10 thread | 431 ms | 232.02 files/s | 0.20% | 15472 KB | Chưa đối chiếu trên Host |
| 200 file / 10 thread | 497 ms | 402.41 files/s | 0.30% | 15540 KB | Chưa đối chiếu trên Host |
| 500 file / 10 thread | 2238 ms | 223.41 files/s | 0.50% | 15880 KB | 500/500 event |

Vì script chạy trên VM có thể không đọc được audit log thật của Central trên
Host, cần kiểm tra event trên Host bằng `Run id`:

```bash
grep -c "stress-500-1783308223-6322" /tmp/oob-audit.log
```

Kết quả đã đo:

```text
500
```

Kết quả demo cần thấy:

- Script chạy đủ 3 mốc `100`, `200`, `500`.
- CPU peak của Agent nhỏ hơn 2%.
- RSS peak của Agent nhỏ hơn 50 MB.
- Agent không crash trong quá trình stress test.
- Với mốc 500 file, Host kiểm tra được `500/500` event trong audit log.

## 10. Checklist Pass/Fail

| Hạng mục | Kỳ vọng |
|---|---|
| Heartbeat | Central nhận heartbeat từ `agent-01` |
| Unauthorized drift | Audit log có `UNAUTHORIZED_DRIFT` |
| Authorized change | Audit log có `AUTHORIZED_CHANGE` trong Deploy Window |
| Jenkins remediation | Jenkins job `webapp` được queue/chạy khi drift |
| Elasticsearch | Index `oob-audit` có document mới |
| Grafana | Dashboard/table hiển thị event mới |
| Performance | CPU Agent < 2%, RSS < 50 MB, Agent không crash |
| Stress event | Mốc 500 file có 500/500 event trên Central |
