#include "CentralConfig.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

CentralConfig CentralConfig::loadFromFile(const QString &path)
{
    CentralConfig cfg;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[Config] Cannot open" << path << "— using defaults / CLI flags";
        return cfg;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull()) {
        qWarning() << "[Config] Invalid JSON in" << path << "— using defaults / CLI flags";
        return cfg;
    }

    const QJsonObject obj = doc.object();

    if (obj.contains("port"))       cfg.port         = obj["port"].toInt(cfg.port);
    if (obj.contains("audit_log"))  cfg.auditLogPath = obj["audit_log"].toString();

    if (obj.contains("elasticsearch")) {
        const QJsonObject es = obj["elasticsearch"].toObject();
        if (es.contains("host"))  cfg.esHost  = es["host"].toString();
        if (es.contains("port"))  cfg.esPort  = es["port"].toInt(cfg.esPort);
        if (es.contains("index")) cfg.esIndex = es["index"].toString();
        if (es.contains("user"))  cfg.esUser  = es["user"].toString();
        if (es.contains("pass"))  cfg.esPass  = es["pass"].toString();
        if (es.contains("https")) cfg.esHttps = es["https"].toBool();
    }

    if (obj.contains("jenkins")) {
        const QJsonObject j = obj["jenkins"].toObject();
        if (j.contains("url"))        cfg.jenkinsUrl        = j["url"].toString();
        if (j.contains("user"))       cfg.jenkinsUser       = j["user"].toString();
        if (j.contains("token"))      cfg.jenkinsToken      = j["token"].toString();
        if (j.contains("ssl_verify")) cfg.jenkinsSslVerify = j["ssl_verify"].toBool();
        if (j.contains("remediate"))  cfg.jenkinsRemediate = j["remediate"].toBool();
        if (j.contains("fail_open"))  cfg.jenkinsFailOpen  = j["fail_open"].toBool();
    }

    if (obj.contains("smtp")) {
        const QJsonObject s = obj["smtp"].toObject();
        if (s.contains("host")) cfg.smtpHost = s["host"].toString();
        if (s.contains("port")) cfg.smtpPort = s["port"].toInt(cfg.smtpPort);
        if (s.contains("user")) cfg.smtpUser = s["user"].toString();
        if (s.contains("pass")) cfg.smtpPass = s["pass"].toString();
        if (s.contains("from")) cfg.smtpFrom = s["from"].toString();
        if (s.contains("to"))   cfg.smtpTo   = s["to"].toString();
    }

    qInfo() << "[Config] Loaded from" << path;
    return cfg;
}
