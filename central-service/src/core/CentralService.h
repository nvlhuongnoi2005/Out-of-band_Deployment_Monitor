#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <thread>

namespace httplib { class Server; }
class AuditLogger;
class ElasticsearchClient;
class IJenkinsClient;
class DeployWindowManager;
class DecisionEngine;
class INotifier;
class AnsibleTrigger;

struct JenkinsConfig {
    QString url;
    QString username;
    QString apiToken;
    bool    enabled() const { return !url.isEmpty(); }
};

class CentralService : public QObject
{
    Q_OBJECT
public:
    explicit CentralService(int port,
                            const QString    &auditLogPath = "/tmp/oob-audit.log",
                            const QString    &esHost       = "",
                            int               esPort       = 9200,
                            const QString    &esIndex      = "oob-audit",
                            const QString    &esUser       = "",
                            const QString    &esPass       = "",
                            bool              esHttps      = false,
                            const JenkinsConfig &jenkins   = {},
                            INotifier        *notifier     = nullptr,
                            AnsibleTrigger   *ansible      = nullptr,
                            QObject          *parent       = nullptr);
    ~CentralService() override;

    bool start();
    void stop();

private:
    void setupRoutes();

    int     m_port;
    bool    m_running = false;
    QString m_auditLogPath;

    std::unique_ptr<httplib::Server>      m_server;
    std::unique_ptr<AuditLogger>          m_auditLogger;
    std::unique_ptr<ElasticsearchClient>  m_esClient;   // nullptr if ES disabled
    std::unique_ptr<IJenkinsClient>       m_jenkins;
    std::unique_ptr<DeployWindowManager>  m_windowManager;
    std::unique_ptr<DecisionEngine>       m_decision;
    INotifier                            *m_notifier = nullptr; // not owned
    AnsibleTrigger                       *m_ansible  = nullptr; // not owned
    std::thread                           m_serverThread;
};
