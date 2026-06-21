# Problem Statement & Use Cases

## 1. Vấn đề cốt lõi

Hệ thống CI/CD hiện tại (Jenkins + Ansible) đảm bảo mọi thay đổi phải qua pipeline
kiểm soát chất lượng (Fortify, SonarQube) trước khi lên production. Tuy nhiên, tồn tại
một lỗ hổng vận hành: **bất kỳ người nào có SSH access vào production server đều có thể
tự ý thay đổi mã nguồn hoặc cấu hình mà không qua pipeline**.

Hành vi này — gọi là **Shadow Deployment** — nguy hiểm vì:

- Bỏ qua toàn bộ bước quét bảo mật (Fortify, SonarQube).
- Không có audit trail rõ ràng.
- Tạo ra khoảng cách (drift) giữa cấu hình chuẩn và thực tế trên server.
- Có thể do con người hoặc mã độc thực hiện.

## 2. Giải pháp

Xây dựng **Reconciliation System** — một hệ thống đối soát hoạt động hoàn toàn độc lập
với luồng CI/CD, liên tục giám sát tính toàn vẹn của file trên production server và
đối chiếu với trạng thái thực thi của Jenkins để phân biệt thay đổi hợp lệ và trái phép.

## 3. Use Cases chính

### UC-01: Jenkins Deploy Hợp Lệ (Authorized Change)

```
Actors   : Jenkins, Ansible, Central Service, Agent
Trigger  : Jenkins kích hoạt job deploy cho project X trên server Y
Main Flow:
  1. Jenkins gửi webhook "DEPLOY_START" về Central Service.
  2. Central Service mở Deploy Window cho (server=Y, project=X).
  3. Ansible SSH vào server Y, copy artifact, restart service.
  4. Agent phát hiện file thay đổi, gửi event về Central Service.
  5. Central Service thấy event nằm trong Deploy Window → AUTHORIZED_CHANGE.
  6. Jenkins gửi webhook "DEPLOY_END" → Central Service đóng Deploy Window.
Kết quả : Log ghi nhận AUTHORIZED_CHANGE, không có alert.
```

### UC-02: User SSH Sửa File Trái Phép (Unauthorized Modify)

```
Actors   : Người dùng có SSH access, Agent, Central Service
Trigger  : User SSH vào server và chỉnh sửa file cấu hình bằng vim/nano/scp
Main Flow:
  1. Không có Deploy Window nào đang mở cho server này.
  2. User sửa file /opt/app/config/db.yaml.
  3. inotify phát hiện sự kiện IN_MODIFY, Agent đọc uid/username từ /proc.
  4. Agent đóng gói event JSON, gửi POST /api/v1/events.
  5. Central Service kiểm tra Deploy Window → không có → UNAUTHORIZED_DRIFT.
  6. Central Service ghi audit log, bắn alert.
Kết quả : Alert trong vòng < 60 giây, audit log có đầy đủ username, path, timestamp.
```

### UC-03: User SSH Xóa File Trái Phép (Unauthorized Delete)

```
Actors   : Người dùng có SSH access, Agent, Central Service
Trigger  : User chạy lệnh rm trên server production
Main Flow:
  1. Không có Deploy Window nào đang mở.
  2. User xóa file /opt/app/config/feature-flag.yaml.
  3. inotify phát hiện IN_DELETE, Agent lấy thông tin process.
  4. Agent gửi event với event_type = "DELETE".
  5. Central Service phân loại → UNAUTHORIZED_DRIFT.
  6. Central Service ghi audit log + optional: trigger Ansible restore.
Kết quả : Alert + log, optional auto-remediation phục hồi file.
```

### UC-04: User chmod/chown Trái Phép (Unauthorized Permission Change)

```
Actors   : Người dùng có SSH access, Agent, Central Service
Trigger  : User chạy chmod 777 hoặc chown trên file production
Main Flow:
  1. Không có Deploy Window nào đang mở.
  2. User chạy: chmod 777 /opt/app/run.sh
  3. inotify phát hiện IN_ATTRIB (attribute change), Agent ghi nhận event.
  4. Agent gửi event với event_type = "ATTRIB".
  5. Central Service phân loại → UNAUTHORIZED_DRIFT.
  6. Central Service ghi audit log + alert.
Kết quả : Alert + log ghi rõ file bị thay đổi permission trái phép.
```

## 4. Thư mục Demo

```
/tmp/demo-prod-app/
├── config/
│   ├── app.yaml
│   └── db.yaml
├── bin/
│   └── app.jar
└── scripts/
    └── run.sh
```

Thư mục này mô phỏng thư mục chạy dự án thực tế trên production server.
Agent sẽ watch toàn bộ cây thư mục này (bao gồm subdirectory đệ quy).
