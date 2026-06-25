#pragma once

#include "IJenkinsClient.h"

// Reads jenkins-state.json to simulate Jenkins API.
// active_jobs array: [{project, server}] — any match → deploy is running.
// Edit the JSON file at runtime to flip between AUTHORIZED and UNAUTHORIZED scenarios.
class MockJenkinsClient : public IJenkinsClient {
public:
    explicit MockJenkinsClient(const QString &stateFilePath);

    bool isDeployRunning(const QString &project, const QString &server) override;

private:
    QString m_stateFilePath;
};
