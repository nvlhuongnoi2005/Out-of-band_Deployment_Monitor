#include "JenkinsRemediator.h"
#include <QProcess>
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <thread>

JenkinsRemediator::JenkinsRemediator(const JenkinsConfig &config, int cooldownSec)
    : m_config(config), m_cooldownSec(cooldownSec)
{}

void JenkinsRemediator::trigger(const QString &server,  const QString &project,
                                 const QString &path,    const QString &eventType)
{
    // Idempotency: one re-deploy per project within the cooldown window.
    // Key is (project) only — one drift event per project is enough to trigger a
    // full re-deploy; additional events within the window will be caught by Jenkins.
    const std::string  key = project.toStdString();
    const std::int64_t now = QDateTime::currentSecsSinceEpoch();

    {
        std::lock_guard<std::mutex> lk(m_lock);
        auto it = m_lastTrigger.find(key);
        if (it != m_lastTrigger.end()) {
            const std::int64_t elapsed = now - it->second;
            if (elapsed < m_cooldownSec) {
                qInfo() << "[Remediation] Skipping — Jenkins re-deploy already triggered for"
                        << project << "(" << elapsed << "s ago, cooldown" << m_cooldownSec << "s)";
                return;
            }
        }
        m_lastTrigger[key] = now;
    }

    qInfo() << "[Remediation] UNAUTHORIZED_DRIFT on" << server << "—"
            << "triggering Jenkins re-deploy for project" << project
            << "| path=" << path << "| event=" << eventType;

    const JenkinsConfig cfg = m_config;

    // Detached thread: the HTTP handler must return immediately so Central can process
    // the next event. Jenkins may take several seconds to accept the build request.
    std::thread([cfg, server, project]() {
        // Jenkins CSRF protection requires a crumb token on every POST request.
        const QString crumbUrl = cfg.url + "/crumbIssuer/api/json";
        QStringList crumbArgs = { "-s", "--globoff",
                                  "-u", cfg.username + ":" + cfg.apiToken,
                                  crumbUrl };
        if (!cfg.sslVerify) crumbArgs << "--insecure";

        QProcess crumbProc;
        crumbProc.start("curl", crumbArgs);
        crumbProc.waitForFinished(10000);

        QString crumbHeader;
        const auto crumbDoc = QJsonDocument::fromJson(crumbProc.readAllStandardOutput());
        if (!crumbDoc.isNull()) {
            const QString field = crumbDoc.object()["crumbRequestField"].toString();
            const QString value = crumbDoc.object()["crumb"].toString();
            if (!field.isEmpty() && !value.isEmpty())
                crumbHeader = field + ":" + value;
        }

        // Parameterized jobs reject POST /build with HTTP 400 — use /buildWithParameters
        // and provide the same parameter names as mock/Jenkinsfile.
        const QString jobUrl = cfg.url + "/job/" + project + "/buildWithParameters";
        QStringList args = {
            "-s", "--globoff",
            "-o", "/dev/null",
            "-w", "%{http_code}",
            "-X", "POST",
            "-u", cfg.username + ":" + cfg.apiToken,
        };
        if (!crumbHeader.isEmpty())
            args << "-H" << crumbHeader;
        if (!cfg.sslVerify)
            args << "--insecure";

        if (!cfg.remediationVmIp.isEmpty())
            args << "--data-urlencode" << ("VM_IP=" + cfg.remediationVmIp);
        if (!cfg.remediationVmUser.isEmpty())
            args << "--data-urlencode" << ("VM_USER=" + cfg.remediationVmUser);
        const QString serverParam = cfg.remediationServer.isEmpty()
                                        ? server
                                        : cfg.remediationServer;
        if (!serverParam.isEmpty())
            args << "--data-urlencode" << ("OOB_SERVER_NAME=" + serverParam);

        args << jobUrl;

        QProcess proc;
        proc.start("curl", args);

        if (!proc.waitForFinished(30000)) {
            qWarning() << "[Remediation] curl timeout — Jenkins re-deploy may not have started"
                       << "| project=" << project;
            proc.kill();
            return;
        }

        const QString httpCode = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();

        // 201 Created = build queued (standard Jenkins response)
        // 200 OK      = some Jenkins versions return 200
        if (httpCode == "201" || httpCode == "200") {
            qInfo() << "[Remediation] Jenkins re-deploy queued for project" << project
                    << "— server" << server << "will be restored by pipeline";
        } else if (httpCode == "403") {
            qWarning() << "[Remediation] Jenkins returned 403 Forbidden — check API token"
                       << "| project=" << project;
        } else if (httpCode == "404") {
            qWarning() << "[Remediation] Jenkins job not found: " << project
                       << "— check that the job name matches the project name";
        } else {
            qWarning() << "[Remediation] Jenkins trigger failed (HTTP" << httpCode << ")"
                       << "| project=" << project;
        }
    }).detach();
}
