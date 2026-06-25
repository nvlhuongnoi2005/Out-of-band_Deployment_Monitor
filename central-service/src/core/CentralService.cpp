#include "CentralService.h"
#include "AuditLogger.h"
#include "elasticsearch/ElasticsearchClient.h"
#include "jenkins/MockJenkinsClient.h"
#include "jenkins/HttpJenkinsClient.h"
#include "window/DeployWindowManager.h"
#include "decision/DecisionEngine.h"
#include "notification/INotifier.h"
#include "remediation/AnsibleTrigger.h"

#include <httplib.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>

static void setJsonError(httplib::Response &res, int status, const QString &message)
{
    res.status = status;
    res.set_content(
        QString(R"({"status":"error","message":"%1"})").arg(message).toStdString(),
        "application/json");
}

static bool parseJsonBody(const httplib::Request &req, httplib::Response &res,
                          QJsonObject &out)
{
    auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(req.body));
    if (doc.isNull() || !doc.isObject()) {
        setJsonError(res, 400, "invalid JSON body");
        return false;
    }
    out = doc.object();
    return true;
}

static bool requireStringFields(const QJsonObject &obj, const QStringList &fields,
                                httplib::Response &res)
{
    for (const QString &f : fields) {
        if (!obj.contains(f) || obj[f].toString().isEmpty()) {
            setJsonError(res, 400, QString("missing required field: %1").arg(f));
            return false;
        }
    }
    return true;
}

static bool requireIntegerFields(const QJsonObject &obj, const QStringList &fields,
                                 httplib::Response &res)
{
    for (const QString &f : fields) {
        if (!obj.contains(f) || !obj[f].isDouble()) {
            setJsonError(res, 400, QString("missing or invalid integer field: %1").arg(f));
            return false;
        }
    }
    return true;
}

static bool isAllowedEventType(const QString &eventType)
{
    static const QStringList allowed = {
        "CREATE", "MODIFY", "DELETE", "ATTRIB", "MOVED_FROM", "MOVED_TO"
    };
    return allowed.contains(eventType);
}

static bool parseDeployTtlSec(const QJsonObject &obj, httplib::Response &res,
                              int &ttlSec)
{
    // Jenkins contract prefers an absolute expiry; ttl_sec remains a demo-friendly fallback.
    if (obj.contains("valid_until")) {
        const QDateTime validUntil =
            QDateTime::fromString(obj["valid_until"].toString(), Qt::ISODate);
        if (!validUntil.isValid()) {
            setJsonError(res, 400, "valid_until must be ISO 8601 datetime");
            return false;
        }
        ttlSec = static_cast<int>(QDateTime::currentDateTime().secsTo(validUntil));
    } else if (obj.contains("ttl_sec")) {
        if (!obj["ttl_sec"].isDouble()) {
            setJsonError(res, 400, "ttl_sec must be an integer");
            return false;
        }
        ttlSec = obj["ttl_sec"].toInt();
    } else {
        setJsonError(res, 400, "OPEN requires valid_until or ttl_sec");
        return false;
    }

    // Keep deploy windows bounded so a bad webhook cannot authorize changes forever.
    if (ttlSec <= 0 || ttlSec > 24 * 60 * 60) {
        setJsonError(res, 400, "deploy window ttl must be between 1 and 86400 seconds");
        return false;
    }
    return true;
}

// ─── Constructor ─────────────────────────────────────────────────────────────

CentralService::CentralService(int port,
                               const QString      &auditLogPath,
                               const QString      &esHost,
                               int                 esPort,
                               const QString      &esIndex,
                               const QString      &esUser,
                               const QString      &esPass,
                               bool                esHttps,
                               const JenkinsConfig &jenkins,
                               INotifier          *notifier,
                               AnsibleTrigger     *ansible,
                               QObject            *parent)
    : QObject(parent)
    , m_port(port)
    , m_auditLogPath(auditLogPath)
    , m_server(std::make_unique<httplib::Server>())
    , m_auditLogger(std::make_unique<AuditLogger>(auditLogPath))
    , m_esClient(esHost.isEmpty() ? nullptr
                                  : std::make_unique<ElasticsearchClient>(
                                        esHost, esPort, esIndex, esUser, esPass, esHttps))
    , m_jenkins(jenkins.enabled()
                    ? static_cast<std::unique_ptr<IJenkinsClient>>(
                          std::make_unique<HttpJenkinsClient>(
                              jenkins.url, jenkins.username, jenkins.apiToken))
                    : std::make_unique<MockJenkinsClient>("mock/jenkins-state.json"))
    , m_windowManager(std::make_unique<DeployWindowManager>())
    , m_decision(std::make_unique<DecisionEngine>(m_jenkins.get(), m_windowManager.get()))
    , m_notifier(notifier)
    , m_ansible(ansible)
{}

CentralService::~CentralService() { stop(); }

// ─── Lifecycle ────────────────────────────────────────────────────────────────

bool CentralService::start()
{
    setupRoutes();
    // bind_to_port() is synchronous — lets us detect port conflicts before returning
    if (!m_server->bind_to_port("0.0.0.0", m_port)) {
        qCritical() << "[Central] Failed to bind on port" << m_port;
        return false;
    }
    m_serverThread = std::thread([this]() { m_server->listen_after_bind(); });
    m_running = true;
    qInfo() << "[Central] HTTP server on port" << m_port;
    qInfo() << "[Central] Audit log    :" << m_auditLogPath;
    qInfo() << "[Central] Elasticsearch:" << (m_esClient ? "enabled" : "disabled");
    qInfo() << "[Central] Email alerts :" << (m_notifier ? "enabled" : "disabled");
    qInfo() << "[Central] Auto-remediation:" << (m_ansible  ? "enabled" : "disabled");
    return true;
}

void CentralService::stop()
{
    if (!m_running) return;
    m_running = false;
    m_server->stop();
    if (m_serverThread.joinable())
        m_serverThread.join();
    qInfo() << "[Central] Stopped.";
}

// ─── Routes ──────────────────────────────────────────────────────────────────

void CentralService::setupRoutes()
{
    m_server->Post("/api/v1/events",
        [this](const httplib::Request &req, httplib::Response &res)
    {
        QJsonObject obj;
        if (!parseJsonBody(req, res, obj)) return;

        static const QStringList required = {
            "event_id", "agent_id", "server", "project", "path",
            "event_type", "timestamp", "username"
        };
        if (!requireStringFields(obj, required, res)) return;
        if (!requireIntegerFields(obj, {"uid"}, res)) return;

        const QString eventId   = obj["event_id"].toString();
        const QString agentId   = obj["agent_id"].toString();
        const QString server    = obj["server"].toString();
        const QString project   = obj["project"].toString();
        const QString path      = obj["path"].toString();
        const QString eventType = obj["event_type"].toString();
        const QString username  = obj["username"].toString();
        const int     uid       = obj["uid"].toInt();
        const int     pid       = obj["pid"].toInt(-1);
        const QString procName  = obj["process_name"].toString("unknown");
        const QString timestamp = obj["timestamp"].toString();

        if (!isAllowedEventType(eventType)) {
            setJsonError(res, 400, "invalid event_type");
            return;
        }

        const Classification cls    = m_decision->classify(project, server);
        const QString        clsStr = classificationToString(cls);

        if (cls == Classification::UNAUTHORIZED_DRIFT) {
            qCritical() << "[Central] *** SHADOW DEPLOYMENT DETECTED ***"
                        << "| server=" + server << "| path=" + path
                        << "| user=" + username << "| proc=" + procName;
        } else {
            qInfo() << "[Central] AUTHORIZED_CHANGE"
                    << "| server=" + server << "| path=" + path;
        }

        QJsonObject audit;
        audit["timestamp"]      = timestamp;
        audit["event_id"]       = eventId;
        audit["agent_id"]       = agentId;
        audit["server"]         = server;
        audit["project"]        = project;
        audit["path"]           = path;
        audit["event_type"]     = eventType;
        audit["uid"]            = uid;
        audit["username"]       = username;
        audit["pid"]            = pid;
        audit["process_name"]   = procName;
        audit["classification"] = clsStr;
        audit["detected_at"]     = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);

        const QString auditJson =
            QString::fromUtf8(QJsonDocument(audit).toJson(QJsonDocument::Compact));

        // Fail closed: if durable audit is down, the Agent should retry this event.
        if (!m_auditLogger->write(auditJson)) {
            qCritical() << "[Central] Audit write failed for event:" << eventId;
            setJsonError(res, 500, "audit write failed");
            return;
        }

        if (m_esClient)
            m_esClient->index(auditJson);

        if (m_ansible && cls == Classification::UNAUTHORIZED_DRIFT)
            m_ansible->trigger(server, project, path, eventType);

        if (m_notifier && cls == Classification::UNAUTHORIZED_DRIFT) {
            const QString subject = QString("[OOB ALERT] Shadow deployment on %1").arg(server);
            const QString body = QString(
                "Shadow deployment detected!\n\n"
                "Server  : %1\nProject : %2\nFile    : %3\n"
                "Action  : %4\nUser    : %5 (uid=%6)\n"
                "Process : %7 (pid=%8)\nTime    : %9\n"
            ).arg(server, project, path, eventType,
                  username, QString::number(uid),
                  procName, QString::number(pid), timestamp);
            m_notifier->notify(subject, body);
        }

        QJsonObject resp;
        resp["status"]         = "received";
        resp["event_id"]       = eventId;
        resp["classification"] = clsStr;
        res.set_content(QJsonDocument(resp).toJson(QJsonDocument::Compact).toStdString(),
                        "application/json");
    });

    m_server->Post("/api/v1/deploy-window",
        [this](const httplib::Request &req, httplib::Response &res)
    {
        QJsonObject obj;
        if (!parseJsonBody(req, res, obj)) return;

        static const QStringList required = {"action", "project", "server"};
        if (!requireStringFields(obj, required, res)) return;

        const QString action  = obj["action"].toString().toUpper();
        const QString project = obj["project"].toString();
        const QString server  = obj["server"].toString();

        if (action == "OPEN") {
            int ttl = 0;
            if (!parseDeployTtlSec(obj, res, ttl)) return;
            m_windowManager->open(project, server, ttl);
            res.set_content(R"({"status":"ok","window":"opened"})", "application/json");
        } else if (action == "CLOSE") {
            m_windowManager->close(project, server);
            res.set_content(R"({"status":"ok","window":"closed"})", "application/json");
        } else {
            setJsonError(res, 400, "action must be OPEN or CLOSE");
        }
    });

    m_server->Post("/api/v1/heartbeat",
        [](const httplib::Request &req, httplib::Response &res)
    {
        QJsonObject obj;
        if (!parseJsonBody(req, res, obj)) return;
        if (!requireStringFields(obj, {"agent_id", "server", "timestamp", "status"}, res))
            return;

        // For now heartbeat is logged only; this is the hook for future online/offline state.
        qInfo() << "[Central] HEARTBEAT"
                << "| agent=" + obj["agent_id"].toString()
                << "| server=" + obj["server"].toString()
                << "| status=" + obj["status"].toString();
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    m_server->Get("/health",
        [](const httplib::Request &, httplib::Response &res)
    {
        res.set_content(R"({"status":"ok","service":"oob-central"})", "application/json");
    });
}
