#pragma once
#include "IJenkinsClient.h"
#include "core/CentralService.h"  // JenkinsConfig

class HttpJenkinsClient : public IJenkinsClient {
public:
    // jobPattern: Jenkins job name must contain the project name
    // e.g. project="webapp" matches job "webapp-deploy"
    explicit HttpJenkinsClient(const JenkinsConfig &config);

    bool isDeployRunning(const QString &project, const QString &server) override;

private:
    JenkinsConfig m_config;
};
