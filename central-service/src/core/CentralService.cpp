#include "CentralService.h"
#include <QDebug>

CentralService::CentralService(int port, QObject *parent)
    : QObject(parent)
    , m_port(port)
{
}

CentralService::~CentralService()
{
    stop();
}

bool CentralService::start()
{
    m_running = true;
    qInfo() << "[Central] Listening on port" << m_port;
    qInfo() << "[Central] Started. HTTP server will be attached on Day 5.";
    return true;
}

void CentralService::stop()
{
    if (!m_running) return;
    m_running = false;
    qInfo() << "[Central] Stopped.";
}
