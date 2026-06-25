#pragma once
#include "INotifier.h"

class SmtpNotifier : public INotifier {
public:
    SmtpNotifier(const QString &host, int port,
                 const QString &username, const QString &password,
                 const QString &from,    const QString &to);

    void notify(const QString &subject, const QString &body) override;

private:
    QString m_host, m_username, m_password, m_from, m_to;
    int     m_port;
};
