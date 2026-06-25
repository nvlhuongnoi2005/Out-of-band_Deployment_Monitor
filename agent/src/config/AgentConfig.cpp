#include "AgentConfig.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

AgentConfig AgentConfig::loadFromFile(const QString &path)
{
    AgentConfig cfg;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[Config] Cannot open" << path << "— using defaults";
        return cfg;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull()) {
        qWarning() << "[Config] Invalid JSON in" << path << "— using defaults";
        return cfg;
    }

    const QJsonObject obj = doc.object();
    if (obj.contains("agent_id"))               cfg.agentId              = obj["agent_id"].toString();
    if (obj.contains("server"))                 cfg.server               = obj["server"].toString();
    if (obj.contains("project"))                cfg.project              = obj["project"].toString();
    if (obj.contains("central_url"))            cfg.centralUrl           = obj["central_url"].toString();
    if (obj.contains("heartbeat_interval_sec")) cfg.heartbeatIntervalSec = obj["heartbeat_interval_sec"].toInt();
    if (obj.contains("retry_interval_sec"))     cfg.retryIntervalSec     = obj["retry_interval_sec"].toInt();

    if (obj.contains("watch_dirs")) {
        cfg.watchDirs.clear();
        for (const auto &v : obj["watch_dirs"].toArray())
            cfg.watchDirs << v.toString();
    }

    return cfg;
}
