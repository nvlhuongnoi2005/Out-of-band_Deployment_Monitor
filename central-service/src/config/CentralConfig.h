#pragma once
#include <QString>

struct CentralConfig {
    int     port         = 8080;
    QString auditLogPath = "/tmp/oob-audit.log";

    // Elasticsearch
    QString esHost;
    int     esPort       = 9200;
    QString esIndex      = "oob-audit";
    QString esUser;
    QString esPass;
    bool    esHttps      = false;

    // Jenkins (status check + auto-remediation)
    QString jenkinsUrl;
    QString jenkinsUser      = "admin";
    QString jenkinsToken;
    QString jenkinsRemediationVmIp;
    QString jenkinsRemediationVmUser;
    QString jenkinsRemediationServer;
    bool    jenkinsSslVerify = true;
    bool    jenkinsRemediate = false;
    // fail_open: when Jenkins API is unreachable, treat as "deploy MAY be running"
    // → AUTHORIZED_CHANGE instead of UNAUTHORIZED_DRIFT.
    // Set true to avoid false alerts when Jenkins is flaky/down during maintenance.
    bool    jenkinsFailOpen  = false;

    // SMTP alerts
    QString smtpHost;
    int     smtpPort = 587;
    QString smtpUser;
    QString smtpPass;
    QString smtpFrom;
    QString smtpTo;

    static CentralConfig loadFromFile(const QString &path);
};
