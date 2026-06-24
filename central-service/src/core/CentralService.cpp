#include "CentralService.h"
#include "AuditLogger.h"

#include <httplib.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

// ─── helpers (file-scope) ────────────────────────────────────────────────────

static QJsonObject parseJsonBody(const httplib::Request &req,
                                 httplib::Response      &res)
{
    auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(req.body));
    if (doc.isNull() || !doc.isObject()) {
        res.status = 400;
        res.set_content(R"({"error":"invalid JSON body"})", "application/json");
        return {};
    }
    return doc.object();
}

static bool requireFields(const QJsonObject  &obj,
                          const QStringList  &fields,
                          httplib::Response   &res)
{
    for (const QString &f : fields) {
        if (!obj.contains(f) || obj[f].toString().isEmpty()) {
            res.status = 400;
            res.set_content(
                QString(R"({"error":"missing required field: %1"})").arg(f).toStdString(),
                "application/json");
            return false;
        }
    }
    return true;
}

// ─── CentralService ──────────────────────────────────────────────────────────

CentralService::CentralService(int port, const QString &auditLogPath, QObject *parent)
    : QObject(parent)
    , m_port(port)
    , m_auditLogPath(auditLogPath)
    , m_server(std::make_unique<httplib::Server>())
    , m_auditLogger(std::make_unique<AuditLogger>(auditLogPath))
{}

CentralService::~CentralService()
{
    stop();
}

bool CentralService::start()
{
    setupRoutes();

    // httplib::Server::listen() blocks the calling thread → run in background
    m_serverThread = std::thread([this]() {
        if (!m_server->listen("0.0.0.0", m_port))
            qCritical() << "[Central] Failed to bind on port" << m_port;
    });

    m_running = true;
    qInfo() << "[Central] HTTP server on port" << m_port;
    qInfo() << "[Central] Audit log:" << m_auditLogPath;
    qInfo() << "[Central] Endpoints:";
    qInfo() << "[Central]   POST /api/v1/events";
    qInfo() << "[Central]   POST /api/v1/heartbeat";
    qInfo() << "[Central]   POST /api/v1/deploy-window";
    qInfo() << "[Central]   GET  /health";
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

// ─── Route registration ──────────────────────────────────────────────────────

void CentralService::setupRoutes()
{
    // ── POST /api/v1/events ──────────────────────────────────────────────────
    m_server->Post("/api/v1/events",
        [this](const httplib::Request &req, httplib::Response &res)
    {
        QJsonObject obj = parseJsonBody(req, res);
        if (obj.isEmpty()) return;

        static const QStringList required = {
            "server", "project", "path", "event_type", "timestamp"
        };
        if (!requireFields(obj, required, res)) {
            qWarning() << "[Central] Rejected event: missing required field";
            return;
        }

        const QString eventId   = obj["event_id"].toString("(no-id)");
        const QString agentId   = obj["agent_id"].toString("unknown");
        const QString server    = obj["server"].toString();
        const QString project   = obj["project"].toString();
        const QString path      = obj["path"].toString();
        const QString eventType = obj["event_type"].toString();
        const QString username  = obj["username"].toString("unknown");
        const int     uid       = obj["uid"].toInt(-1);
        const int     pid       = obj["pid"].toInt(-1);
        const QString procName  = obj["process_name"].toString("unknown");
        const QString timestamp = obj["timestamp"].toString();

        qInfo() << "[Central] EVENT RECEIVED"
                << "| server="   + server
                << "| project="  + project
                << "| type="     + eventType
                << "| path="     + path
                << "| user="     + username
                << "| proc="     + procName
                << "| pid="      + QString::number(pid);

        // Audit log — JSON Lines format for Filebeat → Elasticsearch
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
        audit["classification"] = "PENDING"; // Day 6: Jenkins cross-check replaces this

        m_auditLogger->write(
            QString::fromUtf8(QJsonDocument(audit).toJson(QJsonDocument::Compact)));

        // Response
        QJsonObject resp;
        resp["status"]   = "received";
        resp["event_id"] = eventId;
        res.set_content(
            QJsonDocument(resp).toJson(QJsonDocument::Compact).toStdString(),
            "application/json");
    });

    // ── POST /api/v1/heartbeat ───────────────────────────────────────────────
    m_server->Post("/api/v1/heartbeat",
        [](const httplib::Request &req, httplib::Response &res)
    {
        auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(req.body));
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            qInfo() << "[Central] HEARTBEAT"
                    << "| agent=" + obj["agent_id"].toString()
                    << "| server=" + obj["server"].toString();
        }
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // ── POST /api/v1/deploy-window ───────────────────────────────────────────
    // Day 8 will add DeployWindowManager; for now just ACK and log
    m_server->Post("/api/v1/deploy-window",
        [](const httplib::Request &req, httplib::Response &res)
    {
        auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(req.body));
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            qInfo() << "[Central] DEPLOY-WINDOW"
                    << "| action="  + obj["action"].toString()
                    << "| project=" + obj["project"].toString()
                    << "| server="  + obj["server"].toString();
        }
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // ── GET /health ──────────────────────────────────────────────────────────
    m_server->Get("/health",
        [](const httplib::Request &, httplib::Response &res)
    {
        res.set_content(R"({"status":"ok","service":"oob-central"})",
                        "application/json");
    });
}
