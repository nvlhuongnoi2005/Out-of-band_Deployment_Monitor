#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <thread>

// Forward declare so callers don't need to pull in httplib.h
namespace httplib { class Server; }
class AuditLogger;

class CentralService : public QObject
{
    Q_OBJECT
public:
    explicit CentralService(int port,
                            const QString &auditLogPath = "/tmp/oob-audit.log",
                            QObject *parent = nullptr);
    ~CentralService() override;

    bool start();
    void stop();

private:
    void setupRoutes();

    int     m_port;
    bool    m_running = false;
    QString m_auditLogPath;

    std::unique_ptr<httplib::Server> m_server;
    std::unique_ptr<AuditLogger>     m_auditLogger;
    std::thread                      m_serverThread;
};
