#include "projectio.h"
#include "finalrenderer.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QALYVYRON"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("Tamasrazim"));

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
