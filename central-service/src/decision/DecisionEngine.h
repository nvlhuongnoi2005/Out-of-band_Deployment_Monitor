#pragma once

#include <QString>

class IJenkinsClient;
class DeployWindowManager;

enum class Classification {
    AUTHORIZED_CHANGE,   // Deploy window open OR Jenkins has active job → legitimate
    UNAUTHORIZED_DRIFT,  // Neither → shadow deployment detected
};

inline QString classificationToString(Classification c)
{
    return c == Classification::AUTHORIZED_CHANGE
        ? "AUTHORIZED_CHANGE"
        : "UNAUTHORIZED_DRIFT";
}

class DecisionEngine {
public:
    // Both pointers are not owned — caller manages lifetime.
    explicit DecisionEngine(IJenkinsClient     *jenkinsClient,
                            DeployWindowManager *windowManager);

    Classification classify(const QString &project, const QString &server);

private:
    IJenkinsClient      *m_jenkins; // not owned
    DeployWindowManager *m_window;  // not owned
};
