#include "projectio.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QALYVYRON"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("Tamasrazim"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    ProjectIO projectIO;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("projectIO"), &projectIO);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &application,
        []() { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("Qalyvyron"), QStringLiteral("Main"));
    return application.exec();
}
