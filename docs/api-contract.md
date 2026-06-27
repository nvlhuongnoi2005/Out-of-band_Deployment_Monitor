# API Contract & Event Schema

## 1. Agent → Central Service

### POST /api/v1/events

Agent gửi event này mỗi khi phát hiện thay đổi file.

**Request:**

```
POST /api/v1/events
Content-Type: application/json
```

```json
{
  "event_id":     "550e8400-e29b-41d4-a716-446655440000",
  "agent_id":     "prod-server-01",
  "server":       "prod-server-01",
  "project":      "demo-app",
  "path":         "/tmp/demo-prod-app/config/db.yaml",
  "event_type":   "MODIFY",
  "timestamp":    "2026-06-21T18:30:00.123+07:00",
  "uid":          1001,
  "username":     "devops_john",
  "pid":          23456,
  "process_name": "vim"
}
```

**Trường bắt buộc:**

| Trường | Kiểu | Mô tả |
|---|---|---|
| event_id | string (UUID v4) | ID duy nhất của event |
| agent_id | string | Định danh Agent (tên server) |
| server | string | Tên server production |
| project | string | Tên project đang được giám sát |
| path | string | Đường dẫn tuyệt đối của file thay đổi |
| event_type | enum | CREATE / MODIFY / DELETE / ATTRIB / MOVED_FROM / MOVED_TO |
| timestamp | string (ISO 8601) | Thời điểm xảy ra sự kiện, có timezone |
| uid | integer | UID của user thực hiện thay đổi (lấy từ /proc) |
| username | string | Tên user (tra từ /etc/passwd theo uid); "unknown" nếu không xác định được |

**Trường tùy chọn:**

| Trường | Kiểu | Mô tả |
|---|---|---|
| pid | integer | PID của process thực hiện thay đổi |
| process_name | string | Tên process (ví dụ: vim, scp, python3) |

**Response thành công:**

```
HTTP/1.1 200 OK
Content-Type: application/json
```

```json
{
  "status": "received",
  "event_id": "550e8400-e29b-41d4-a716-446655440000"
}
```

**Response lỗi:**

```
HTTP/1.1 400 Bad Request
```

```json
{
  "status": "error",
  "message": "Missing required field: path"
}
```

---

### POST /api/v1/heartbeat

Agent gửi heartbeat định kỳ (mỗi 30 giây) để báo hiệu Agent đang online.

**Request:**

```json
{
  "agent_id":  "prod-server-01",
  "timestamp": "2026-06-21T18:30:00+07:00",
  "status":    "healthy"
}
```

**Response:**

```json
{
  "status": "ok"
}
```

---

## 2. Jenkins → Central Service

### POST /api/v1/deploy-window

Jenkins gửi thông báo khi bắt đầu và kết thúc một job deploy,
để Central Service mở/đóng Deploy Window tránh false positive.

**Request — Mở Deploy Window:**

```json
{
  "server":      "prod-server-01",
  "project":     "demo-app",
  "action":      "OPEN",
  "valid_until": "2026-06-21T18:40:00+07:00"
}
```

Hoặc dùng TTL tương đối (tiện khi test thủ công):

```json
{
  "server":  "prod-server-01",
  "project": "demo-app",
  "action":  "OPEN",
  "ttl_sec": 600
}
```

**Request — Đóng Deploy Window:**

```json
{
  "server":  "prod-server-01",
  "project": "demo-app",
  "action":  "CLOSE"
}
```

**Trường bắt buộc:**

| Trường | Kiểu | Mô tả |
|---|---|---|
| server | string | Server đang được deploy |
| project | string | Project đang được deploy |
| action | enum | OPEN / CLOSE |
| valid_until | string (ISO 8601) | (OPEN, hoặc dùng ttl_sec) Thời điểm hết hạn tuyệt đối |
| ttl_sec | integer | (OPEN, thay thế valid_until) Số giây Deploy Window tồn tại |

> **Lưu ý:** `valid_until` nên đặt = thời gian dự kiến deploy xong + buffer 2 phút.
> Central Service sẽ tự đóng Deploy Window khi hết hạn TTL dù Jenkins không gửi CLOSE.

**Response:**

```json
{
  "status": "ok",
  "window": "opened"
}
```

---

## 3. Audit Log Schema (Central Service ghi ra file)

Mỗi dòng trong file audit log là một JSON object (JSON Lines format).

### Trường hợp AUTHORIZED_CHANGE

```json
{
  "event_id":       "550e8400-e29b-41d4-a716-446655440000",
  "agent_id":       "agent-prod-01",
  "server":         "prod-server-01",
  "project":        "demo-app",
  "path":           "/tmp/demo-prod-app/config/db.yaml",
  "event_type":     "MODIFY",
  "timestamp":      "2026-06-21T11:30:00.123Z",
  "uid":            0,
  "username":       "ansible",
  "pid":            9876,
  "process_name":   "python3",
  "classification": "AUTHORIZED_CHANGE",
  "detected_at":    "2026-06-21T11:30:00.890Z"
}
```

### Trường hợp UNAUTHORIZED_DRIFT

```json
{
  "event_id":       "661f9511-f30c-52e5-b827-557766551111",
  "agent_id":       "agent-prod-01",
  "server":         "prod-server-01",
  "project":        "demo-app",
  "path":           "/tmp/demo-prod-app/config/db.yaml",
  "event_type":     "MODIFY",
  "timestamp":      "2026-06-21T12:00:00.456Z",
  "uid":            1001,
  "username":       "devops_john",
  "pid":            23456,
  "process_name":   "vim",
  "classification": "UNAUTHORIZED_DRIFT",
  "detected_at":    "2026-06-21T12:00:01.120Z"
}
```

> **Ghi chú:** Tất cả timestamps dùng UTC với suffix `Z`. Central Service không ghi `jenkins_job`/`jenkins_build` vào audit log — classification được quyết định bởi Deploy Window + Jenkins job color (`_anime`), kết quả chỉ là `AUTHORIZED_CHANGE` hoặc `UNAUTHORIZED_DRIFT`.

**Mô tả các trường audit log:**

| Trường | Kiểu | Mô tả |
|---|---|---|
| event_id | string | UUID, ID gốc từ Agent |
| agent_id | string | Định danh agent gửi event |
| server | string | Tên server nơi xảy ra thay đổi |
| project | string | Tên project/application |
| path | string | Đường dẫn tuyệt đối của file thay đổi |
| event_type | string | CREATE / MODIFY / DELETE / ATTRIB / MOVED_FROM / MOVED_TO |
| timestamp | string | UTC ISO 8601, thời điểm xảy ra trên server |
| uid | int | Unix user ID của process thực hiện thay đổi |
| username | string | Tên user tương ứng uid |
| pid | int | Process ID, -1 nếu không xác định được |
| process_name | string | Tên tiến trình, "unknown" nếu không xác định được |
| classification | string | AUTHORIZED_CHANGE hoặc UNAUTHORIZED_DRIFT |
| detected_at | string | UTC ISO 8601, thời điểm Central quyết định xong |

---

## 4. Audit Log — vị trí file

File mặc định: `/tmp/oob-audit.log` (có thể cấu hình qua `audit_log` trong JSON config hoặc `--audit-log` CLI flag). Khi triển khai production, nên đặt thành `/var/log/oob-audit.log` để logrotate quản lý.

Format: JSON Lines — mỗi event là một dòng JSON, dễ đọc bằng `jq` hoặc `grep`.

Central Service tự ghi file này sau mỗi classification và đẩy event lên Elasticsearch qua HTTP (không dùng Filebeat). Logrotate quản lý rotation và nén (`/etc/logrotate.d/oob-audit`).

