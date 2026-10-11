#include "projectio.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace
{
constexpr qint64 kMaximumProjectBytes = 64 * 1024 * 1024;
constexpr int kMaximumSceneObjects = 10000;
constexpr auto kFormatName = "qalyvyron.scene";
constexpr int kSupportedMajorVersion = 1;

QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), message}};
}

QString localPath(const QUrl &url)
{
    if (url.isLocalFile())
        return url.toLocalFile();

    // Accept a plain local path from trusted local UI callers as well.
    if (url.scheme().isEmpty())
        return url.toString();

    return {};
}

bool validDocument(const QJsonObject &object, QString *reason)
{
    if (object.value(QStringLiteral("format")).toString() != QLatin1String(kFormatName)) {
        *reason = QStringLiteral("This is not a QALYVYRON scene file.");
        return false;
    }

    const int version = object.value(QStringLiteral("version")).toInt(-1);
    if (version < 1 || version > kSupportedMajorVersion) {
        *reason = QStringLiteral("This scene uses an unsupported project format version.");
        return false;
    }

    const QJsonValue objectsValue = object.value(QStringLiteral("objects"));
    if (!objectsValue.isArray()) {
        *reason = QStringLiteral("The scene file does not contain a valid object list.");
        return false;
    }

    if (objectsValue.toArray().size() > kMaximumSceneObjects) {
        *reason = QStringLiteral("The scene contains too many objects to open safely.");
        return false;
    }

    return true;
}
}

ProjectIO::ProjectIO(QObject *parent)
    : QObject(parent)
{
}

QVariantMap ProjectIO::saveProject(const QUrl &url,
                                   const QVariantMap &document) const
{
    const QString path = localPath(url);
    if (path.isEmpty())
        return failure(QStringLiteral("Choose a local file path to save the scene."));

    const QJsonDocument json = QJsonDocument::fromVariant(document);
    if (json.isNull() || !json.isObject())
        return failure(QStringLiteral("The scene could not be converted to JSON."));

    QString validationError;
    if (!validDocument(json.object(), &validationError))
        return failure(validationError);

    const QByteArray bytes = json.toJson(QJsonDocument::Indented);
    if (bytes.size() > kMaximumProjectBytes)
        return failure(QStringLiteral("The scene file exceeds the 64 MiB safety limit."));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return failure(QStringLiteral("Could not open the selected file for writing: %1")
                           .arg(file.errorString()));

    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return failure(QStringLiteral("The scene could not be fully written to disk."));
    }

    if (!file.commit())
        return failure(QStringLiteral("Could not finish saving the scene: %1")
                           .arg(file.errorString()));

    return {{QStringLiteral("ok"), true},
            {QStringLiteral("path"), QFileInfo(path).absoluteFilePath()}};
}

QVariantMap ProjectIO::loadProject(const QUrl &url) const
{
    const QString path = localPath(url);
    if (path.isEmpty())
        return failure(QStringLiteral("Choose a local QALYVYRON scene file."));

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return failure(QStringLiteral("The selected scene file does not exist."));

    if (info.size() < 1 || info.size() > kMaximumProjectBytes)
        return failure(QStringLiteral("The scene file is empty or exceeds the 64 MiB safety limit."));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("Could not open the scene file: %1")
                           .arg(file.errorString()));

    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return failure(QStringLiteral("Could not read the complete scene file."));

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
        return failure(QStringLiteral("The selected file is not valid JSON: %1")
                           .arg(parseError.errorString()));

    QString validationError;
    if (!validDocument(json.object(), &validationError))
        return failure(validationError);

    return {{QStringLiteral("ok"), true},
            {QStringLiteral("data"), json.object().toVariantMap()},
            {QStringLiteral("path"), info.absoluteFilePath()}};
}
