# Kịch Bản Test Hệ Thống

Tài liệu này mô tả kịch bản test cho Out-of-Band Deployment Monitor theo yêu cầu
FR/NFR trong `docs/requirements.md`. Mục tiêu là chứng minh hệ thống bắt được
thay đổi file, phân loại đúng deploy hợp lệ và drift trái phép, ghi audit log,
retry khi Central tạm mất kết nối, và đạt các chỉ số vận hành cơ bản.

## 1. Phạm vi test

| Nhóm | Mục tiêu | Yêu cầu liên quan |
|---|---|---|
| Smoke test | Build được và Central trả health OK | Ràng buộc kỹ thuật |
| Functional test | Agent phát hiện file event và gửi về Central | FR-01, FR-02, FR-03, FR-05 |
| Decision test | Central phân loại đúng AUTHORIZED_CHANGE / UNAUTHORIZED_DRIFT | FR-06, FR-07 |
| Audit/alert test | Ghi JSON Lines và phát cảnh báo/remediation khi drift | FR-08, FR-09, FR-12 |
| Reliability test | Agent retry khi Central tạm dừng | FR-04, NFR-05 |
| Non-functional test | Đo latency, CPU, RSS và false positive | NFR-01, NFR-02, NFR-03 |

## 2. Môi trường test đề xuất

| Thành phần | Cấu hình |
|---|---|
| Host chạy Central | Linux, Qt 6, CMake, cổng 8080 |
| VM/host chạy Agent | Linux, thư mục watch `/tmp/demo-prod-app` |
| Audit log | `/tmp/oob-audit.log` |
| Agent config | `mock/agent-config.json` |
| Central config | Mặc định CLI cho test core; dùng `mock/central-config.json.example` nếu test tích hợp |
| Optional | Jenkins, Elasticsearch, Grafana, SMTP nếu test tích hợp mở rộng |

Chuẩn bị thư mục demo:

```bash
mkdir -p /tmp/demo-prod-app/config
```

Build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Chạy Central ở Terminal 1:

```bash
./build/central-service/oob-central --audit-log /tmp/oob-audit.log
```

Chạy Agent bằng inotify ở Terminal 2:

```bash
./build/agent/oob-agent --config mock/agent-config.json --watcher inotify
```

Chạy Agent bằng eBPF nếu có `bpftrace` và quyền root:

```bash
sudo ./build/agent/oob-agent --config mock/agent-config.json --watcher ebpf
```

## 3. Dữ liệu kiểm tra chung

| Trường | Giá trị mẫu |
|---|---|
| server | `local-demo` hoặc `prod-server-01` |
| project | `webapp` |
| watch dir | `/tmp/demo-prod-app` |
| file mẫu | `/tmp/demo-prod-app/config/app.json` |
| audit log | `/tmp/oob-audit.log` |

Lệnh theo dõi audit log:

```bash
tail -f /tmp/oob-audit.log
```

Kiểm tra nhanh dòng log mới nhất:

```bash
tail -n 1 /tmp/oob-audit.log
```

## 4. Test Case Chi Tiết

### TC-01 - Build và health check Central

**Mục tiêu:** xác nhận project build được và Central expose health endpoint.

**Bước chạy:**

Terminal 1:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/central-service/oob-central --audit-log /tmp/oob-audit.log
```

Terminal 2:

```bash
curl -s http://127.0.0.1:8080/health
```

**Kết quả mong đợi:**

```json
{"status":"ok","service":"oob-central"}
```

**Pass khi:** build không lỗi, Central chạy được, health trả HTTP 200.

### TC-02 - Agent phát hiện CREATE/MODIFY/DELETE/ATTRIB

**Mục tiêu:** xác nhận Agent bắt các thay đổi filesystem trong thư mục watch.

**Bước chạy:**

```bash
mkdir -p /tmp/demo-prod-app/config
echo '{"version":"1.0.0"}' > /tmp/demo-prod-app/config/app.json
echo '{"version":"1.0.1"}' > /tmp/demo-prod-app/config/app.json
chmod 777 /tmp/demo-prod-app/config/app.json
rm /tmp/demo-prod-app/config/app.json
```

**Kết quả mong đợi:**

- Audit log có event tương ứng `CREATE`, `MODIFY`, `ATTRIB`, `DELETE`.
- Mỗi event có `uid`, `username`, `pid`, `process_name`.
- Response từ Central có `status:"received"`.

**Pass khi:** các event xuất hiện trong `/tmp/oob-audit.log` trong vòng 5 giây.

### TC-03 - Phân loại shadow deployment là UNAUTHORIZED_DRIFT

**Mục tiêu:** xác nhận thay đổi ngoài deploy window bị xem là drift trái phép.

**Tiền điều kiện:** không mở deploy window cho `project=webapp`, `server=local-demo`.

**Bước chạy:**

```bash
echo '{"version":"HACKED"}' > /tmp/demo-prod-app/config/app.json
```

**Kết quả mong đợi trong audit log:**

```json
"classification":"UNAUTHORIZED_DRIFT"
```

**Pass khi:** event được ghi log với classification `UNAUTHORIZED_DRIFT`.

### TC-04 - Mở deploy window bằng ttl_sec

**Mục tiêu:** xác nhận Central mở cửa sổ deploy hợp lệ bằng TTL tương đối.

**Bước chạy:**

```bash
curl -s -X POST http://127.0.0.1:8080/api/v1/deploy-window \
  -H "Content-Type: application/json" \
  -d '{"action":"OPEN","project":"webapp","server":"local-demo","ttl_sec":120}'
```

**Kết quả mong đợi:**

```json
{"status":"ok","window":"opened"}
```

**Pass khi:** Central trả HTTP 200 và `window:"opened"`.

### TC-05 - Phân loại deploy hợp lệ là AUTHORIZED_CHANGE

**Mục tiêu:** xác nhận thay đổi trong deploy window không bị báo drift.

**Tiền điều kiện:** TC-04 đã mở deploy window.

**Bước chạy:**

```bash
echo '{"version":"2.0.0","deployed_by":"jenkins"}' > /tmp/demo-prod-app/config/app.json
```

Đóng deploy window sau khi test:

```bash
curl -s -X POST http://127.0.0.1:8080/api/v1/deploy-window \
  -H "Content-Type: application/json" \
  -d '{"action":"CLOSE","project":"webapp","server":"local-demo"}'
```

**Kết quả mong đợi trong audit log:**

```json
"classification":"AUTHORIZED_CHANGE"
```

**Pass khi:** event trong deploy window được phân loại `AUTHORIZED_CHANGE` và
không có alert drift.

### TC-06 - Deploy window hết hạn tự động

**Mục tiêu:** xác nhận Central không giữ deploy window quá TTL.

**Bước chạy:**

```bash
curl -s -X POST http://127.0.0.1:8080/api/v1/deploy-window \
  -H "Content-Type: application/json" \
  -d '{"action":"OPEN","project":"webapp","server":"local-demo","ttl_sec":3}'

sleep 5
echo '{"version":"AFTER_TTL"}' > /tmp/demo-prod-app/config/app.json
```

**Kết quả mong đợi:** event sau khi TTL hết hạn có
`classification:"UNAUTHORIZED_DRIFT"`.

**Pass khi:** không còn được phân loại `AUTHORIZED_CHANGE` sau khi TTL hết hạn.

### TC-07 - Validate request lỗi ở /api/v1/events

**Mục tiêu:** xác nhận Central reject event thiếu field hoặc sai event type.

**Bước chạy:**

```bash
curl -i -X POST http://127.0.0.1:8080/api/v1/events \
  -H "Content-Type: application/json" \
  -d '{"event_id":"bad-1","agent_id":"a1"}'
```

Test event type sai:

```bash
curl -i -X POST http://127.0.0.1:8080/api/v1/events \
  -H "Content-Type: application/json" \
  -d '{"event_id":"bad-2","agent_id":"a1","server":"local-demo","project":"webapp","path":"/tmp/demo-prod-app/x","event_type":"HACK","timestamp":"2026-06-29T00:00:00Z","uid":1000,"username":"lap"}'
```

**Kết quả mong đợi:**

- HTTP 400.
- Body có `status:"error"`.
- Với event type sai: message là `invalid event_type`.

**Pass khi:** Central không ghi audit log cho payload lỗi.

### TC-08 - Heartbeat Agent

**Mục tiêu:** xác nhận Central nhận heartbeat định kỳ từ Agent.

**Bước chạy thủ công qua API:**

```bash
curl -s -X POST http://127.0.0.1:8080/api/v1/heartbeat \
  -H "Content-Type: application/json" \
  -d '{"agent_id":"local-demo","server":"local-demo","timestamp":"2026-06-29T00:00:00Z","status":"healthy"}'
```

**Kết quả mong đợi:**

```json
{"status":"ok"}
```

**Pass khi:** API trả OK; khi chạy Agent thật, Central log heartbeat mỗi 30 giây.

### TC-09 - Retry khi Central tạm mất kết nối

**Mục tiêu:** xác nhận Agent giữ event trong memory queue và gửi lại khi Central
online trở lại.

**Bước chạy:**

1. Chạy Agent và Central bình thường.
2. Dừng Central bằng `Ctrl+C`.
3. Tạo nhiều thay đổi file:

```bash
echo retry-1 > /tmp/demo-prod-app/config/retry-1.txt
echo retry-2 > /tmp/demo-prod-app/config/retry-2.txt
echo retry-3 > /tmp/demo-prod-app/config/retry-3.txt
```

4. Quan sát log Agent có thông báo retry theo `retry_interval_sec`.
5. Chạy lại Central:

```bash
./build/central-service/oob-central --audit-log /tmp/oob-audit.log
```

**Kết quả mong đợi:**

- Khi Central down, Agent log `Failed` và `retry`.
- Khi Central online lại, các event được gửi tiếp.
- Audit log có đủ các file `retry-1.txt`, `retry-2.txt`, `retry-3.txt`.

**Pass khi:** không mất event trong khoảng dừng Central của bài test.

**Lưu ý:** hiện repo chưa có script tự động cho test này; đây là kịch bản thủ công
nên ghi lại thời gian dừng Central và số event tạo ra trong biên bản test.

### TC-10 - Audit log fail closed

**Mục tiêu:** xác nhận nếu Central không ghi được audit log thì trả lỗi để Agent
retry, tránh mất dấu sự kiện.

**Bước chạy:**

Chạy Central với audit log trỏ vào thư mục không tồn tại:

```bash
./build/central-service/oob-central --audit-log /tmp/oob-missing-dir/oob-audit.log
```

Gửi event mẫu:

```bash
curl -i -X POST http://127.0.0.1:8080/api/v1/events \
  -H "Content-Type: application/json" \
  -d '{"event_id":"audit-fail-1","agent_id":"local-demo","server":"local-demo","project":"webapp","path":"/tmp/demo-prod-app/config/app.json","event_type":"MODIFY","timestamp":"2026-06-29T00:00:00Z","uid":1000,"username":"lap","pid":1234,"process_name":"curl"}'
```

**Kết quả mong đợi:**

- HTTP 500.
- Body có `message:"audit write failed"`.

**Pass khi:** Central không trả success khi audit log không bền vững.

### TC-11 - Đo độ trễ phát hiện

**Mục tiêu:** xác nhận thời gian từ khi file thay đổi đến khi có audit log nhỏ
hơn 60 giây.

**Bước chạy:**

```bash
./scripts/measure-latency.sh 5
```

**Kết quả mong đợi:**

- Script in `RESULT  : PASS`.
- `Max` nhỏ hơn `60 000 ms`.

**Pass khi:** toàn bộ sample hoặc phần lớn sample hợp lệ không timeout.

### TC-12 - Đo tài nguyên Agent

**Mục tiêu:** xác nhận Agent tiêu thụ tài nguyên thấp khi tải bình thường.

**Bước chạy:**

```bash
./scripts/measure-resources.sh 60
```

**Kết quả mong đợi:**

- CPU peak `< 2%`.
- RSS peak `< 51200 KB`.

**Pass khi:** script in PASS cho CPU và RSS.

### TC-13 - Elasticsearch/Grafana

**Mục tiêu:** xác nhận audit event được đẩy sang Elasticsearch để Grafana đọc.

**Tiền điều kiện:** bật `elasticsearch` trong Central config và ES đang chạy.

**Bước chạy:**

```bash
curl http://127.0.0.1:9200/oob-audit/_count
curl "http://127.0.0.1:9200/oob-audit/_search?pretty&size=3"
```

**Kết quả mong đợi:**

- Index `oob-audit` có document mới.
- Document có `@timestamp`, `classification`, `server`, `project`, `path`.

**Pass khi:** Grafana datasource Elasticsearch truy vấn được dữ liệu theo
`@timestamp`.

### TC-14 - Auto-remediation qua Jenkins

**Mục tiêu:** xác nhận khi phát hiện `UNAUTHORIZED_DRIFT`, Central trigger Jenkins
để khôi phục file.

**Tiền điều kiện:**

- Central bật `jenkins.remediate=true`.
- Jenkins job name khớp `project`, ví dụ `webapp`.
- Jenkins job có parameter `VM_IP`, `VM_USER`, `OOB_SERVER_NAME`.
- Jenkins node/container có `ansible-playbook` và SSH client.
- Jenkins credential SSH có ID `vm-deploy-key`.

**Bước chạy:**

```bash
echo '{"version":"HACKED_FOR_REMEDIATION"}' > /tmp/demo-prod-app/config/app.json
```

**Kết quả mong đợi:**

- Audit log ghi `UNAUTHORIZED_DRIFT`.
- Central gọi Jenkins build/remediation.
- Jenkins mở deploy window và gọi Ansible restore file.
- Event restore sau đó được ghi `AUTHORIZED_CHANGE`.

**Pass khi:** Jenkins job chạy và file được khôi phục về trạng thái chuẩn.

## 5. Ma trận pass/fail tối thiểu

| Test case | Bắt buộc để demo MVP | Trạng thái kỳ vọng |
|---|---:|---|
| TC-01 Build + health | Có | PASS |
| TC-02 File event | Có | PASS |
| TC-03 Unauthorized drift | Có | PASS |
| TC-04 Deploy window open | Có | PASS |
| TC-05 Authorized change | Có | PASS |
| TC-06 TTL expire | Có | PASS |
| TC-07 Request validation | Có | PASS |
| TC-08 Heartbeat | Nên có | PASS |
| TC-09 Retry disconnect | Có | PASS hoặc ghi rõ đang test thủ công |
| TC-10 Audit fail closed | Nên có | PASS |
| TC-11 Latency | Có | PASS, max < 60s |
| TC-12 Resource | Có | PASS, CPU/RSS trong ngưỡng |
| TC-13 Elasticsearch/Grafana | Khuyến khích | PASS nếu bật ES/Grafana |
| TC-14 Jenkins remediation | Khuyến khích | PASS nếu có Jenkins thật |

## 6. Mẫu biên bản test

| Trường | Nội dung |
|---|---|
| Ngày test | |
| Người test | |
| Commit/branch | |
| OS/Kernel | |
| Watcher backend | `inotify` / `ebpf` |
| Central config | |
| Agent config | |
| Tổng số case | |
| PASS | |
| FAIL | |
| Ghi chú | |

Mẫu ghi kết quả từng case:

| Test case | Kết quả | Bằng chứng |
|---|---|---|
| TC-03 | PASS | Audit log có `classification:"UNAUTHORIZED_DRIFT"` |
| TC-05 | PASS | Audit log có `classification:"AUTHORIZED_CHANGE"` trong deploy window |
| TC-11 | PASS | `measure-latency.sh`: max = ... ms |
| TC-12 | PASS | `measure-resources.sh`: CPU = ... %, RSS = ... KB |

## 7. Rủi ro và giới hạn khi test

- Inotify có thể không lấy chính xác `pid/process_name` với process quá ngắn do
  race condition sau khi file event xảy ra; eBPF cho kết quả tốt hơn nếu môi
  trường hỗ trợ.
- Retry hiện là in-memory queue; nếu Agent process chết trong lúc Central down
  thì event chưa gửi có thể mất. Đây là giới hạn cần nêu khi báo cáo.
- Elasticsearch, Grafana, SMTP và Jenkins là phần tích hợp ngoài; nếu môi trường
  demo chưa dựng đủ, nên đánh dấu là test tích hợp chưa chạy thay vì báo fail
  core Agent/Central.
- Môi trường sandbox hoặc máy local có thể làm port/runtime probe không ổn định;
  khi đó ưu tiên bằng chứng build, API response, audit log và log process.
