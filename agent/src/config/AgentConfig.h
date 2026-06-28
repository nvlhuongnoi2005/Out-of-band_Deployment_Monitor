#pragma once
#include <QString>
#include <QStringList>

struct AgentConfig {
    QString     agentId              = "local-demo";
    QString     server               = "local-demo";
    QString     project              = "demo-app";
    QStringList watchDirs            = { "/tmp/demo-prod-app" };
    QString     centralUrl           = "http://127.0.0.1:8080";
    int         heartbeatIntervalSec = 30;
    int         retryIntervalSec     = 5;
    QString     watcherBackend       = "inotify";

    static AgentConfig loadFromFile(const QString &path);
};
