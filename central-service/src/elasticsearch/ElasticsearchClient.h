#pragma once
#include <QString>

class ElasticsearchClient {
public:
    explicit ElasticsearchClient(const QString &host, int port,
                                 const QString &index,
                                 const QString &username = "",
                                 const QString &password = "",
                                 bool           useHttps = false);

    bool index(const QString &jsonDoc);

private:
    QString m_host, m_index, m_username, m_password;
    int     m_port;
    bool    m_useHttps;
};
