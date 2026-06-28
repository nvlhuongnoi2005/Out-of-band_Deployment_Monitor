#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include "config/CentralConfig.h"
#include "core/CentralService.h"
#include "notification/SmtpNotifier.h"
#include "remediation/JenkinsRemediator.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("oob-central");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Out-of-Band Deployment Monitor — Central Service");
    parser.addHelpOption();
    parser.addVersionOption();

    // Config file (mọi setting có thể đặt ở đây thay vì gõ flags dài)
    QCommandLineOption configOption({"c", "config"},
        "Path to JSON config file (all settings can be set here)", "path", "");

    // CLI flags — override giá trị từ config file nếu được gõ tường minh
    QCommandLineOption portOption({"p", "port"}, "Port to listen on", "port", "");
    QCommandLineOption auditOption({"a", "audit-log"}, "Audit log path", "path", "");

    QCommandLineOption esHostOption("es-host",  "Elasticsearch host", "host", "");
    QCommandLineOption esPortOption("es-port",  "Elasticsearch port", "port", "");
    QCommandLineOption esIndexOption("es-index","Elasticsearch index", "index", "");
    QCommandLineOption esUserOption("es-user",  "Elasticsearch username", "user", "");
    QCommandLineOption esPassOption("es-pass",  "Elasticsearch password", "pass", "");
    QCommandLineOption esHttpsOption("es-https","Use HTTPS for Elasticsearch");

    QCommandLineOption jenkinsUrlOption("jenkins-url",
        "Jenkins base URL, e.g. http://localhost:8081", "url", "");
    QCommandLineOption jenkinsUserOption("jenkins-user", "Jenkins username", "user", "");
    QCommandLineOption jenkinsTokenOption("jenkins-token", "Jenkins API token", "token", "");
    QCommandLineOption jenkinsInsecureOption("jenkins-insecure",
        "Skip TLS cert verification for Jenkins (self-signed certs)");
    QCommandLineOption jenkinsRemediateOption("jenkins-remediate",
        "Enable auto-remediation: re-trigger Jenkins pipeline on UNAUTHORIZED_DRIFT");

    QCommandLineOption smtpHostOption("smtp-host", "SMTP host", "host", "");
    QCommandLineOption smtpPortOption("smtp-port", "SMTP port", "port", "");
    QCommandLineOption smtpUserOption("smtp-user", "SMTP username", "user", "");
    QCommandLineOption smtpPassOption("smtp-pass", "SMTP password", "pass", "");
    QCommandLineOption smtpFromOption("smtp-from", "Sender address", "email", "");
    QCommandLineOption smtpToOption("smtp-to",   "Admin alert address", "email", "");

    parser.addOption(configOption);
    parser.addOption(portOption);
    parser.addOption(auditOption);
    parser.addOption(esHostOption);
    parser.addOption(esPortOption);
    parser.addOption(esIndexOption);
    parser.addOption(esUserOption);
    parser.addOption(esPassOption);
    parser.addOption(esHttpsOption);
    parser.addOption(jenkinsUrlOption);
    parser.addOption(jenkinsUserOption);
    parser.addOption(jenkinsTokenOption);
    parser.addOption(jenkinsInsecureOption);
    parser.addOption(jenkinsRemediateOption);
    parser.addOption(smtpHostOption);
    parser.addOption(smtpPortOption);
    parser.addOption(smtpUserOption);
    parser.addOption(smtpPassOption);
    parser.addOption(smtpFromOption);
    parser.addOption(smtpToOption);
    parser.process(app);

    qInfo() << "=== Out-of-Band Deployment Monitor — Central Service v0.1.0 ===";

    // Load config file first (nếu có), sau đó CLI flags override từng field
    CentralConfig cfg;
    if (parser.isSet(configOption))
        cfg = CentralConfig::loadFromFile(parser.value(configOption));

    // CLI overrides — chỉ áp dụng khi flag được gõ tường minh
    if (parser.isSet(portOption))           cfg.port              = parser.value(portOption).toInt();
    if (parser.isSet(auditOption))          cfg.auditLogPath      = parser.value(auditOption);
    if (parser.isSet(esHostOption))         cfg.esHost            = parser.value(esHostOption);
    if (parser.isSet(esPortOption))         cfg.esPort            = parser.value(esPortOption).toInt();
    if (parser.isSet(esIndexOption))        cfg.esIndex           = parser.value(esIndexOption);
    if (parser.isSet(esUserOption))         cfg.esUser            = parser.value(esUserOption);
    if (parser.isSet(esPassOption))         cfg.esPass            = parser.value(esPassOption);
    if (parser.isSet(esHttpsOption))        cfg.esHttps           = true;
    if (parser.isSet(jenkinsUrlOption))     cfg.jenkinsUrl        = parser.value(jenkinsUrlOption);
    if (parser.isSet(jenkinsUserOption))    cfg.jenkinsUser       = parser.value(jenkinsUserOption);
    if (parser.isSet(jenkinsTokenOption))   cfg.jenkinsToken      = parser.value(jenkinsTokenOption);
    if (parser.isSet(jenkinsInsecureOption)) cfg.jenkinsSslVerify = false;
    if (parser.isSet(jenkinsRemediateOption)) cfg.jenkinsRemediate = true;
    if (parser.isSet(smtpHostOption))       cfg.smtpHost          = parser.value(smtpHostOption);
    if (parser.isSet(smtpPortOption))       cfg.smtpPort          = parser.value(smtpPortOption).toInt();
    if (parser.isSet(smtpUserOption))       cfg.smtpUser          = parser.value(smtpUserOption);
    if (parser.isSet(smtpPassOption))       cfg.smtpPass          = parser.value(smtpPassOption);
    if (parser.isSet(smtpFromOption))       cfg.smtpFrom          = parser.value(smtpFromOption);
    if (parser.isSet(smtpToOption))         cfg.smtpTo            = parser.value(smtpToOption);

    // Jenkins config
    JenkinsConfig jenkins;
    jenkins.url        = cfg.jenkinsUrl;
    jenkins.username   = cfg.jenkinsUser;
    jenkins.apiToken   = cfg.jenkinsToken;
    jenkins.remediationVmIp     = cfg.jenkinsRemediationVmIp;
    jenkins.remediationVmUser   = cfg.jenkinsRemediationVmUser;
    jenkins.remediationServer   = cfg.jenkinsRemediationServer;
    jenkins.sslVerify  = cfg.jenkinsSslVerify;
    jenkins.failOpen   = cfg.jenkinsFailOpen;

    // Auto-remediation
    std::unique_ptr<JenkinsRemediator> remediator;
    if (cfg.jenkinsRemediate) {
        if (!jenkins.enabled()) {
            qCritical() << "jenkins.remediate=true requires jenkins.url to be set";
            return 1;
        }
        remediator = std::make_unique<JenkinsRemediator>(jenkins);
    }

    // SMTP notifier
    std::unique_ptr<SmtpNotifier> notifier;
    if (!cfg.smtpHost.isEmpty()) {
        notifier = std::make_unique<SmtpNotifier>(
            cfg.smtpHost, cfg.smtpPort,
            cfg.smtpUser, cfg.smtpPass,
            cfg.smtpFrom, cfg.smtpTo);
    }

    CentralService service(
        cfg.port,
        cfg.auditLogPath,
        cfg.esHost, cfg.esPort, cfg.esIndex, cfg.esUser, cfg.esPass, cfg.esHttps,
        jenkins,
        notifier.get(),
        remediator.get());

    if (!service.start())
        return 1;

    return app.exec();
}
