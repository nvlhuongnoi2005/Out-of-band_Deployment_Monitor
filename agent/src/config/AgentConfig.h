#pragma once

#include <QString>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDebug>

struct AgentConfig {
    QString     agentId             = "local-demo";
    QString     server              = "local-demo";
    QString     project             = "demo-app";
    QStringList watchDirs           = { "/tmp/demo-prod-app" };
    QString     centralUrl          = "http://127.0.0.1:8080";
    int         heartbeatIntervalSec = 30;
    int         retryIntervalSec     = 5;

    static AgentConfig loadFromFile(const QString &path)
    {
        AgentConfig cfg;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "[Config] Cannot open" << path << "— using defaults";
            return cfg;
        }

        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isNull()) {
            qWarning() << "[Config] Invalid JSON in" << path << "— using defaults";
            return cfg;
        }

        QJsonObject obj = doc.object();
        if (obj.contains("agent_id"))               cfg.agentId = obj["agent_id"].toString();
        if (obj.contains("server"))                 cfg.server  = obj["server"].toString();
        if (obj.contains("project"))                cfg.project = obj["project"].toString();
        if (obj.contains("central_url"))            cfg.centralUrl = obj["central_url"].toString();
        if (obj.contains("heartbeat_interval_sec")) cfg.heartbeatIntervalSec = obj["heartbeat_interval_sec"].toInt();
        if (obj.contains("retry_interval_sec"))     cfg.retryIntervalSec = obj["retry_interval_sec"].toInt();

        if (obj.contains("watch_dirs")) {
            cfg.watchDirs.clear();
            for (const auto &v : obj["watch_dirs"].toArray())
                cfg.watchDirs << v.toString();
        }

        return cfg;
    }
};
