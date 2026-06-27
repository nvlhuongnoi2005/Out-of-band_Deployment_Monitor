#pragma once
#include "core/CentralService.h"  // JenkinsConfig
#include <mutex>
#include <unordered_map>
#include <cstdint>

// FR-12: Auto-remediation by re-triggering the Jenkins pipeline.
//
// When UNAUTHORIZED_DRIFT is detected, we POST to Jenkins to re-run the
// deployment job. Jenkins re-applies the known-good state via its pipeline
// (Ansible / scripts), which is the authoritative source of truth.
//
// Why call Jenkins instead of running Ansible directly:
//   - Jenkins owns the deployment — it has the correct parameters and secrets
//   - The Jenkinsfile already opens a Deploy Window before deploying, so the
//     re-deploy events are classified as AUTHORIZED_CHANGE, not drift
//   - Jenkins provides a build log and history for audit purposes
//
// Jenkins API: POST /job/{project}/build
// Auth: HTTP Basic with username + API token
// HTTPS: supported (curl backend), use sslVerify=false for self-signed certs
class JenkinsRemediator {
public:
    explicit JenkinsRemediator(const JenkinsConfig &config, int cooldownSec = 300);

    // Trigger a Jenkins re-deploy for the given project.
    // Fire-and-forget: runs in a detached thread, does not block the caller.
    // Idempotency: same project is not re-triggered within cooldownSec seconds.
    void trigger(const QString &server,  const QString &project,
                 const QString &path,    const QString &eventType);

private:
    JenkinsConfig m_config;
    int           m_cooldownSec;

    std::mutex                                    m_lock;
    std::unordered_map<std::string, std::int64_t> m_lastTrigger; // project → epoch_sec
};
