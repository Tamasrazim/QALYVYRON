#include "projectio.h"
#include "finalrenderer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

namespace
{
void applicationMessageHandler(QtMsgType type,
                               const QMessageLogContext &context,
                               const QString &message)
{
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    static QFile logFile;
    static bool initialized = false;
    if (!initialized) {
        QString path = qEnvironmentVariable("QALYVYRON_LOG_FILE");
        if (path.isEmpty()) {
            const QString directory =
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
            QDir().mkpath(directory);
            path = directory + QStringLiteral("/qalyvyron.log");
        } else {
            QDir().mkpath(QFileInfo(path).absolutePath());
        }
        logFile.setFileName(path);
        logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        initialized = true;
    }

    if (logFile.isOpen()) {
        const char *level = "INFO";
        switch (type) {
        case QtDebugMsg: level = "DEBUG"; break;
        case QtInfoMsg: level = "INFO"; break;
        case QtWarningMsg: level = "WARNING"; break;
        case QtCriticalMsg: level = "CRITICAL"; break;
        case QtFatalMsg: level = "FATAL"; break;
        }
        QTextStream output(&logFile);
        output << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
               << ' ' << level << ": " << message;
        if (context.file)
            output << " (" << context.file << ':' << context.line << ')';
        output << Qt::endl;
    }
}
} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QALYVYRON"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("Tamasrazim"));
    qInstallMessageHandler(applicationMessageHandler);

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    ProjectIO projectIO;
    FinalRenderer finalRenderer;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("projectIO"), &projectIO);
    engine.rootContext()->setContextProperty(QStringLiteral("finalRenderer"), &finalRenderer);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &application,
        []() { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Qalyvyron/Main.qml")));
    return application.exec();
}
