#pragma once

#include <QString>

// Strategy interface — Central can use HttpJenkinsClient or nullptr without changing DecisionEngine.
class IJenkinsClient {
public:
    virtual ~IJenkinsClient() = default;

    // Returns true if Jenkins has an active deploy job for this project on this server
    virtual bool isDeployRunning(const QString &project, const QString &server) = 0;
};
