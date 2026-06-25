#include "DecisionEngine.h"
#include "jenkins/IJenkinsClient.h"
#include "window/DeployWindowManager.h"
#include <QDebug>

DecisionEngine::DecisionEngine(IJenkinsClient     *jenkinsClient,
                               DeployWindowManager *windowManager)
    : m_jenkins(jenkinsClient)
    , m_window(windowManager)
{}

Classification DecisionEngine::classify(const QString &project, const QString &server)
{
    // Deploy window takes priority: Ansible pre-announced a deploy → trust it
    if (m_window->isOpen(project, server))
        return Classification::AUTHORIZED_CHANGE;

    // Fallback: check if Jenkins has an active job right now
    if (m_jenkins->isDeployRunning(project, server))
        return Classification::AUTHORIZED_CHANGE;

    return Classification::UNAUTHORIZED_DRIFT;
}
