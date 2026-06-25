#pragma once
#include "IJenkinsClient.h"

class HttpJenkinsClient : public IJenkinsClient {
public:
    // jobPattern: Jenkins job name must contain the project name
    // e.g. project="webapp" matches job "webapp-deploy"
    explicit HttpJenkinsClient(const QString &baseUrl,
                               const QString &username,
                               const QString &apiToken);

    bool isDeployRunning(const QString &project, const QString &server) override;

private:
    QString m_baseUrl, m_username, m_apiToken;
};
