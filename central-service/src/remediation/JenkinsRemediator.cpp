#include "JenkinsRemediator.h"

#include <QProcess>
#include <QDateTime>
#include <QDebug>
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

    std::thread([cfg, server, project]() {
        // Jenkins REST API: POST /job/{name}/build
        // Convention: Jenkins job name = project name (e.g. "webapp")
        const QString jobUrl = cfg.url + "/job/" + project + "/build";

        QStringList args = {
            "-s",
            "-o", "/dev/null",           // discard body
            "-w", "%{http_code}",        // write HTTP status code to stdout
            "-X", "POST",
            "-u", cfg.username + ":" + cfg.apiToken,
            jobUrl
        };

        // Skip TLS certificate verification for self-signed / internal CA certs.
        if (!cfg.sslVerify)
            args << "--insecure";

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
