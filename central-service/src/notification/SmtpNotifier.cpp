#include "SmtpNotifier.h"

#include <QProcess>
#include <QTemporaryFile>
#include <QDir>
#include <QDebug>

SmtpNotifier::SmtpNotifier(const QString &host, int port,
                            const QString &username, const QString &password,
                            const QString &from,     const QString &to)
    : m_host(host), m_port(port)
    , m_username(username), m_password(password)
    , m_from(from), m_to(to)
{}

void SmtpNotifier::notify(const QString &subject, const QString &body)
{
    // Encode subject/body as base64 — avoids all escaping issues with newlines/quotes
    const QString subjectB64 = QString::fromLatin1(subject.toUtf8().toBase64());
    const QString bodyB64    = QString::fromLatin1(body.toUtf8().toBase64());

    const QString script = QString(
        "import smtplib, ssl, base64\n"
        "from email.mime.text import MIMEText\n"
        "subject = base64.b64decode('%1').decode()\n"
        "body    = base64.b64decode('%2').decode()\n"
        "msg = MIMEText(body)\n"
        "msg['Subject'] = subject\n"
        "msg['From']    = '%3'\n"
        "msg['To']      = '%4'\n"
        "ctx = ssl.create_default_context()\n"
        "with smtplib.SMTP('%5', %6) as s:\n"
        "    s.starttls(context=ctx)\n"
        "    s.login('%7', '%8')\n"
        "    s.sendmail('%3', '%4', msg.as_string())\n"
        "print('sent')\n"
    )
    .arg(subjectB64, bodyB64, m_from, m_to,
         m_host, QString::number(m_port),
         m_username, m_password);

    // Write script to a temp file — avoids credentials appearing in `ps aux` process list
    QTemporaryFile scriptFile(QDir::tempPath() + "/oob-smtp-XXXXXX.py");
    if (!scriptFile.open()) {
        qWarning() << "[SMTP] Cannot create temp script file";
        return;
    }
    scriptFile.write(script.toUtf8());
    scriptFile.flush();
    const QString scriptPath = scriptFile.fileName();

    QProcess proc;
    proc.start("python3", {scriptPath});

    if (!proc.waitForFinished(10000)) {
        qWarning() << "[SMTP] Timeout sending alert";
        proc.kill();
        return;
    }

    if (proc.exitCode() != 0)
        qWarning() << "[SMTP] Failed:" << proc.readAllStandardError().simplified();
    else
        qInfo() << "[SMTP] Alert sent ->" << m_to << "|" << subject;
}
