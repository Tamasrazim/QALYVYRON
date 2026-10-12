// What this does: Reads and writes QALYVYRON scene files and OBJ geometry, checking file contents and sizes before accepting them.

#include "projectio.h"

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QQuaternion>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QTextStream>
#include <QVector3D>
#include <QtEndian>

#include <cmath>
#include <utility>

namespace
{
constexpr qint64 kMaximumProjectBytes = 64 * 1024 * 1024;
constexpr qint64 kMaximumObjBytes = 128 * 1024 * 1024;
constexpr qint64 kMaximumObjExportBytes = 256 * 1024 * 1024;
constexpr qint64 kMaximumStlBytes = 128 * 1024 * 1024;
constexpr quint32 kMaximumStlTriangles = 100000;
constexpr int kMaximumSceneObjects = 10000;
constexpr int kMaximumMeshVertices = 100000;
constexpr int kMaximumMeshFaces = 100000;
constexpr int kMaximumFaceCorners = 4096;
constexpr qint64 kMaximumRenderedVertices = 1000000;
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

QVector3D faceNormal(const QVector<int> &face, const QVector<QVector3D> &vertices)
{
    QVector3D normal;
    if (face.size() < 3)
        return normal;

    for (int i = 0; i < face.size(); ++i) {
        const QVector3D &a = vertices.at(face.at(i));
        const QVector3D &b = vertices.at(face.at((i + 1) % face.size()));
        normal.setX(normal.x() + (a.y() - b.y()) * (a.z() + b.z()));
        normal.setY(normal.y() + (a.z() - b.z()) * (a.x() + b.x()));
        normal.setZ(normal.z() + (a.x() - b.x()) * (a.y() + b.y()));
    }
    return normal;
}

bool parseNumber(const QString &token, double *value)
{
    bool converted = false;
    const double parsed = token.toDouble(&converted);
    if (!converted || !std::isfinite(parsed) || std::abs(parsed) > 1.0e6)
        return false;
    *value = parsed;
    return true;
}

bool decodeMesh(const QVariantMap &mesh,
                QVector<QVector3D> *vertices,
                QVector<QVector<int>> *faces,
                QString *reason)
{
    const QVariantList rawVertices = mesh.value(QStringLiteral("vertices")).toList();
    const QVariantList rawFaces = mesh.value(QStringLiteral("faces")).toList();
    if (rawVertices.size() < 3 || rawVertices.size() > kMaximumMeshVertices
        || rawFaces.isEmpty() || rawFaces.size() > kMaximumMeshFaces) {
        *reason = QStringLiteral("Mesh is empty or exceeds the supported topology limits.");
        return false;
    }

    vertices->clear();
    vertices->reserve(rawVertices.size());
    for (const QVariant &rawVertex : rawVertices) {
        const QVariantList coordinates = rawVertex.toList();
        if (coordinates.size() != 3) {
            *reason = QStringLiteral("Mesh contains a vertex that does not have three coordinates.");
            return false;
        }

        double values[3] = {};
        for (int axis = 0; axis < 3; ++axis) {
            bool converted = false;
            values[axis] = coordinates.at(axis).toDouble(&converted);
            if (!converted || !std::isfinite(values[axis]) || std::abs(values[axis]) > 1.0e6) {
                *reason = QStringLiteral("Mesh contains a non-finite or out-of-range vertex.");
                return false;
            }
        }

        vertices->append(QVector3D(static_cast<float>(values[0]),
                                   static_cast<float>(values[1]),
                                   static_cast<float>(values[2])));
    }

    faces->clear();
    faces->reserve(rawFaces.size());
    qint64 renderedVertexCount = 0;
    for (const QVariant &rawFace : rawFaces) {
        const QVariantList rawCorners = rawFace.toList();
        if (rawCorners.size() < 3 || rawCorners.size() > kMaximumFaceCorners) {
            *reason = QStringLiteral("Mesh contains a face outside the supported corner-count limits.");
            return false;
        }

        renderedVertexCount += static_cast<qint64>(rawCorners.size() - 2) * 3;
        if (renderedVertexCount > kMaximumRenderedVertices) {
            *reason = QStringLiteral("Mesh would expand beyond the safe rendering budget.");
            return false;
        }

        QVector<int> face;
        face.reserve(rawCorners.size());
        QSet<int> uniqueCorners;
        for (const QVariant &rawIndex : rawCorners) {
            bool converted = false;
            const int index = rawIndex.toInt(&converted);
            if (!converted || index < 0 || index >= vertices->size()
                || uniqueCorners.contains(index)) {
                *reason = QStringLiteral("Mesh contains an invalid or repeated face vertex index.");
                return false;
            }
            uniqueCorners.insert(index);
            face.append(index);
        }

        const QVector3D normal = faceNormal(face, *vertices);
        if (!std::isfinite(normal.x()) || !std::isfinite(normal.y())
            || !std::isfinite(normal.z()) || normal.lengthSquared() < 1.0e-10f) {
            *reason = QStringLiteral("Mesh contains a degenerate face.");
            return false;
        }

        faces->append(face);
    }

    return true;
}

QString safeObjName(QString name)
{
    name = name.trimmed();
    for (QChar &character : name) {
        if (!(character.isLetterOrNumber() || character == QLatin1Char('_')
              || character == QLatin1Char('-'))) {
            character = QLatin1Char('_');
        }
    }
    return name.isEmpty() ? QStringLiteral("Object") : name;
}

struct ExportObject
{
    QString name;
    QVector<QVector3D> vertices;
    QVector<QVector<int>> faces;
    QVector3D position;
    QQuaternion rotation;
    QVector3D scale{1.0f, 1.0f, 1.0f};
};
}

ProjectIO::ProjectIO(QObject *parent)
    : QObject(parent)
{
}

bool ProjectIO::showWelcomeOnStartup() const
{
    const QSettings settings;
    return settings.value(QStringLiteral("interface/showWelcomeOnStartup"), true).toBool();
}

void ProjectIO::setShowWelcomeOnStartup(bool show) const
{
    QSettings settings;
    settings.setValue(QStringLiteral("interface/showWelcomeOnStartup"), show);
    settings.sync();
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

QVariantMap ProjectIO::importObj(const QUrl &url) const
{
    const QString path = localPath(url);
    if (path.isEmpty())
        return failure(QStringLiteral("Choose a local OBJ file."));
    
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()
        || info.suffix().compare(QStringLiteral("obj"), Qt::CaseInsensitive) != 0) {
        return failure(QStringLiteral("The selected file is not a Wavefront OBJ file."));
    }
    if (info.size() < 1 || info.size() > kMaximumObjBytes)
        return failure(QStringLiteral("The OBJ file is empty or exceeds the 128 MiB import limit."));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return failure(QStringLiteral("Could not open the OBJ file: %1").arg(file.errorString()));

    const QRegularExpression whitespace(QStringLiteral("\\s+"));
    QVector<QVector3D> vertices;
    QVector<QVector<int>> faces;
    qint64 renderedVertexCount = 0;
    QTextStream input(&file);
    int lineNumber = 0;

    while (!input.atEnd()) {
        const QString line = input.readLine();
        ++lineNumber;
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;

        const QStringList tokens = trimmed.split(whitespace, Qt::SkipEmptyParts);
        if (tokens.isEmpty())
            continue;

        if (tokens.first() == QLatin1String("v")) {
            if (tokens.size() < 4)
                return failure(QStringLiteral("OBJ line %1 has an incomplete vertex.").arg(lineNumber));
            if (vertices.size() >= kMaximumMeshVertices)
                return failure(QStringLiteral("OBJ exceeds the 100,000-vertex limit."));

            double xyz[3] = {};
            for (int axis = 0; axis < 3; ++axis) {
                if (!parseNumber(tokens.at(axis + 1), &xyz[axis]))
                    return failure(QStringLiteral("OBJ line %1 contains an invalid vertex coordinate.").arg(lineNumber));
            }
            vertices.append(QVector3D(static_cast<float>(xyz[0]),
                                      static_cast<float>(xyz[1]),
                                      static_cast<float>(xyz[2])));
        } else if (tokens.first() == QLatin1String("f")) {
            if (tokens.size() < 4 || tokens.size() > kMaximumFaceCorners + 1)
                return failure(QStringLiteral("OBJ line %1 has an unsupported face size.").arg(lineNumber));
            if (faces.size() >= kMaximumMeshFaces)
                return failure(QStringLiteral("OBJ exceeds the 100,000-face limit."));

            QVector<int> face;
            face.reserve(tokens.size() - 1);
            QSet<int> uniqueCorners;
            for (int i = 1; i < tokens.size(); ++i) {
                const QString indexToken = tokens.at(i).section(QLatin1Char('/'), 0, 0);
                bool converted = false;
                const qint64 rawIndex = indexToken.toLongLong(&converted);
                if (!converted || rawIndex == 0)
                    return failure(QStringLiteral("OBJ line %1 contains an invalid face index.").arg(lineNumber));

                const qint64 index = rawIndex > 0
                    ? rawIndex - 1
                    : static_cast<qint64>(vertices.size()) + rawIndex;
                if (index < 0 || index >= vertices.size()
                    || uniqueCorners.contains(static_cast<int>(index))) {
                    return failure(QStringLiteral("OBJ line %1 references a missing or repeated vertex.").arg(lineNumber));
                }
                uniqueCorners.insert(static_cast<int>(index));
                face.append(static_cast<int>(index));
            }

            renderedVertexCount += static_cast<qint64>(face.size() - 2) * 3;
            if (renderedVertexCount > kMaximumRenderedVertices)
                return failure(QStringLiteral("OBJ exceeds the safe rendered-mesh size limit."));

            const QVector3D normal = faceNormal(face, vertices);
            if (!std::isfinite(normal.x()) || !std::isfinite(normal.y())
                || !std::isfinite(normal.z()) || normal.lengthSquared() < 1.0e-10f) {
                return failure(QStringLiteral("OBJ line %1 defines a degenerate face.").arg(lineNumber));
            }
            faces.append(face);
        }
        // Texture coordinates, normals, material libraries, and groups are ignored.
        // The vertex indices and polygon topology are sufficient for the editable mesh.
    }

    if (file.error() != QFileDevice::NoError)
        return failure(QStringLiteral("An I/O error occurred while reading the OBJ file."));
    if (vertices.size() < 3 || faces.isEmpty())
        return failure(QStringLiteral("The OBJ file contains no editable polygon mesh."));

    QVariantList serializedVertices;
    serializedVertices.reserve(vertices.size());
    for (const QVector3D &vertex : vertices) {
        serializedVertices.append(QVariant::fromValue(QVariantList{
            static_cast<double>(vertex.x()),
            static_cast<double>(vertex.y()),
            static_cast<double>(vertex.z())
        }));
    }

    QVariantList serializedFaces;
    serializedFaces.reserve(faces.size());
    for (const QVector<int> &face : faces) {
        QVariantList corners;
        corners.reserve(face.size());
        for (const int index : face)
            corners.append(index);
        serializedFaces.append(QVariant::fromValue(corners));
    }

    const QVariantMap mesh{
        {QStringLiteral("shape"), QStringLiteral("Custom")},
        {QStringLiteral("vertices"), serializedVertices},
        {QStringLiteral("faces"), serializedFaces}
    };

    return {{QStringLiteral("ok"), true},
            {QStringLiteral("mesh"), mesh},
            {QStringLiteral("label"), info.completeBaseName()},
            {QStringLiteral("path"), info.absoluteFilePath()}};
}


QVariantMap ProjectIO::importStl(const QUrl &url) const
{
    const QString path = localPath(url);
    if (path.isEmpty())
        return failure(QStringLiteral("Choose a local STL file."));

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()
        || info.suffix().compare(QStringLiteral("stl"), Qt::CaseInsensitive) != 0) {
        return failure(QStringLiteral("The selected file is not an STL mesh."));
    }
    if (info.size() < 1 || info.size() > kMaximumStlBytes)
        return failure(QStringLiteral("The STL file is empty or exceeds the 128 MiB import limit."));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("Could not open the STL file: %1").arg(file.errorString()));

    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return failure(QStringLiteral("An I/O error occurred while reading the STL file."));
    if (bytes.isEmpty())
        return failure(QStringLiteral("The STL file is empty."));

    QVector<QVector3D> vertices;
    QVector<QVector<int>> faces;
    QHash<QString, int> vertexIndices;

    // STL repeats corner coordinates for every triangle. Weld identical coordinates
    // into one editable vertex so neighboring triangles share real mesh edges.
    auto indexForVertex = [&](const QVector3D &vertex) -> int {
        const QString key = QString::number(vertex.x(), 'g', 9) + QLatin1Char(',')
            + QString::number(vertex.y(), 'g', 9) + QLatin1Char(',')
            + QString::number(vertex.z(), 'g', 9);
        const auto found = vertexIndices.constFind(key);
        if (found != vertexIndices.cend())
            return found.value();
        if (vertices.size() >= kMaximumMeshVertices)
            return -1;
        const int index = vertices.size();
        vertices.append(vertex);
        vertexIndices.insert(key, index);
        return index;
    };

    auto validVertex = [](const QVector3D &vertex) {
        return std::isfinite(vertex.x()) && std::isfinite(vertex.y())
            && std::isfinite(vertex.z())
            && std::abs(vertex.x()) <= 1.0e6f
            && std::abs(vertex.y()) <= 1.0e6f
            && std::abs(vertex.z()) <= 1.0e6f;
    };

    QString parseError;
    auto appendTriangle = [&](const QVector3D &a,
                              const QVector3D &b,
                              const QVector3D &c,
                              quint32 triangleNumber) -> bool {
        if (!validVertex(a) || !validVertex(b) || !validVertex(c)) {
            parseError = QStringLiteral("STL triangle %1 contains a non-finite or out-of-range vertex.")
                             .arg(triangleNumber);
            return false;
        }
        if (faces.size() >= static_cast<int>(kMaximumStlTriangles)) {
            parseError = QStringLiteral("STL exceeds the 100,000-triangle edit limit.");
            return false;
        }

        const int aIndex = indexForVertex(a);
        const int bIndex = indexForVertex(b);
        const int cIndex = indexForVertex(c);
        if (aIndex < 0 || bIndex < 0 || cIndex < 0) {
            parseError = QStringLiteral("STL exceeds the 100,000-unique-vertex edit limit.");
            return false;
        }

        const QVector<int> face{aIndex, bIndex, cIndex};
        const QVector3D normal = faceNormal(face, vertices);
        if (!std::isfinite(normal.x()) || !std::isfinite(normal.y())
            || !std::isfinite(normal.z()) || normal.lengthSquared() < 1.0e-10f) {
            parseError = QStringLiteral("STL triangle %1 is degenerate.").arg(triangleNumber);
            return false;
        }
        faces.append(face);
        return true;
    };

    // Binary STL is 80 header bytes, a 32-bit triangle count, then 50 bytes per triangle.
    // Checking the exact length also avoids mistaking common ASCII files starting with "solid".
    bool binaryStl = false;
    quint32 binaryTriangleCount = 0;
    if (bytes.size() >= 84) {
        binaryTriangleCount = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(bytes.constData() + 80));
        const qint64 expectedSize = 84LL + static_cast<qint64>(binaryTriangleCount) * 50LL;
        binaryStl = binaryTriangleCount > 0
            && binaryTriangleCount <= kMaximumStlTriangles
            && expectedSize == bytes.size();
    }

    if (binaryStl) {
        QByteArray mutableBytes = bytes;
        QDataStream input(&mutableBytes, QIODevice::ReadOnly);
        input.setByteOrder(QDataStream::LittleEndian);
        input.setFloatingPointPrecision(QDataStream::SinglePrecision);

        char header[80];
        if (input.readRawData(header, 80) != 80)
            return failure(QStringLiteral("The binary STL header is incomplete."));
        quint32 declaredCount = 0;
        input >> declaredCount;
        if (input.status() != QDataStream::Ok || declaredCount != binaryTriangleCount)
            return failure(QStringLiteral("The binary STL triangle count is invalid."));

        for (quint32 triangle = 0; triangle < declaredCount; ++triangle) {
            // Triangle normals are ignored because the editable mesh recomputes its own.
            if (input.skipRawData(12) != 12)
                return failure(QStringLiteral("Binary STL triangle %1 has a truncated normal.").arg(triangle + 1));

            QVector3D points[3];
            for (int corner = 0; corner < 3; ++corner) {
                float x = 0.0f, y = 0.0f, z = 0.0f;
                input >> x >> y >> z;
                if (input.status() != QDataStream::Ok)
                    return failure(QStringLiteral("Binary STL triangle %1 is truncated.").arg(triangle + 1));
                points[corner] = QVector3D(x, y, z);
            }
            if (input.skipRawData(2) != 2)
                return failure(QStringLiteral("Binary STL triangle %1 has a truncated attribute field.").arg(triangle + 1));

            if (!appendTriangle(points[0], points[1], points[2], triangle + 1))
                return failure(parseError);
        }
    } else {
        const QByteArray trimmed = bytes.trimmed();
        if (trimmed.left(5).compare("solid", Qt::CaseInsensitive) != 0) {
            return failure(QStringLiteral("The STL file has an invalid binary length and does not look like an ASCII STL."));
        }

        QByteArray mutableBytes = bytes;
        QTextStream input(&mutableBytes, QIODevice::ReadOnly);
        QVector<QVector3D> pendingVertices;
        pendingVertices.reserve(3);
        quint32 triangleNumber = 0;
        int lineNumber = 0;

        while (!input.atEnd()) {
            const QString line = input.readLine().trimmed();
            ++lineNumber;
            const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                                  Qt::SkipEmptyParts);
            if (tokens.isEmpty()
                || tokens.first().compare(QStringLiteral("vertex"), Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (tokens.size() < 4) {
                return failure(QStringLiteral("ASCII STL line %1 has an incomplete vertex.")
                                   .arg(lineNumber));
            }

            double xyz[3] = {};
            for (int axis = 0; axis < 3; ++axis) {
                if (!parseNumber(tokens.at(axis + 1), &xyz[axis])) {
                    return failure(QStringLiteral("ASCII STL line %1 contains an invalid vertex coordinate.")
                                       .arg(lineNumber));
                }
            }
            pendingVertices.append(QVector3D(static_cast<float>(xyz[0]),
                                             static_cast<float>(xyz[1]),
                                             static_cast<float>(xyz[2])));
            if (pendingVertices.size() == 3) {
                ++triangleNumber;
                if (!appendTriangle(pendingVertices.at(0), pendingVertices.at(1),
                                    pendingVertices.at(2), triangleNumber)) {
                    return failure(parseError);
                }
                pendingVertices.clear();
            }
        }

        if (!pendingVertices.isEmpty())
            return failure(QStringLiteral("ASCII STL ends with an incomplete triangle."));
    }

    if (faces.isEmpty())
        return failure(QStringLiteral("The STL file contains no editable triangles."));

    QVariantList serializedVertices;
    serializedVertices.reserve(vertices.size());
    for (const QVector3D &vertex : vertices) {
        serializedVertices.append(QVariant::fromValue(QVariantList{
            static_cast<double>(vertex.x()),
            static_cast<double>(vertex.y()),
            static_cast<double>(vertex.z())
        }));
    }

    QVariantList serializedFaces;
    serializedFaces.reserve(faces.size());
    for (const QVector<int> &face : faces) {
        QVariantList corners;
        corners.reserve(face.size());
        for (const int index : face)
            corners.append(index);
        serializedFaces.append(QVariant::fromValue(corners));
    }

    const QVariantMap mesh{
        {QStringLiteral("shape"), QStringLiteral("Custom")},
        {QStringLiteral("vertices"), serializedVertices},
        {QStringLiteral("faces"), serializedFaces}
    };

    return {{QStringLiteral("ok"), true},
            {QStringLiteral("mesh"), mesh},
            {QStringLiteral("label"), info.completeBaseName()},
            {QStringLiteral("path"), info.absoluteFilePath()},
            {QStringLiteral("format"), QStringLiteral("stl")}};
}

QVariantMap ProjectIO::exportObj(const QUrl &url,
                                 const QVariantMap &document) const
{
    const QString path = localPath(url);
    if (path.isEmpty())
        return failure(QStringLiteral("Choose a local OBJ output path."));

    const QVariantList objects = document.value(QStringLiteral("objects")).toList();
    if (objects.isEmpty() || objects.size() > kMaximumSceneObjects)
        return failure(QStringLiteral("There are no scene objects to export, or the scene is too large."));

    QVector<ExportObject> exportObjects;
    int skippedObjects = 0;
    for (const QVariant &rawObject : objects) {
        const QVariantMap object = rawObject.toMap();
        if (object.contains(QStringLiteral("visible"))
            && !object.value(QStringLiteral("visible")).toBool()) {
            ++skippedObjects;
            continue;
        }
        if (object.value(QStringLiteral("kind")).toString() == QLatin1String("asset")) {
            ++skippedObjects;
            continue;
        }

        const QVariantMap mesh = object.value(QStringLiteral("mesh")).toMap();
        if (mesh.isEmpty()) {
            ++skippedObjects;
            continue;
        }

        ExportObject out;
        QString meshError;
        if (!decodeMesh(mesh, &out.vertices, &out.faces, &meshError)) {
            return failure(QStringLiteral("Cannot export %1: %2")
                               .arg(object.value(QStringLiteral("label")).toString(),
                                    meshError));
        }

        auto readTransform = [&object](const QString &key, double fallback,
                                       double minimum, double maximum,
                                       double *value) -> bool {
            if (!object.contains(key)) {
                *value = fallback;
                return true;
            }
            bool converted = false;
            const double parsed = object.value(key).toDouble(&converted);
            if (!converted || !std::isfinite(parsed)
                || parsed < minimum || parsed > maximum) {
                return false;
            }
            *value = parsed;
            return true;
        };

        double x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0, scale = 1;
        double scaleX = 1, scaleY = 1, scaleZ = 1;
        if (!readTransform(QStringLiteral("x"), 0, -1.0e6, 1.0e6, &x)
            || !readTransform(QStringLiteral("y"), 0, -1.0e6, 1.0e6, &y)
            || !readTransform(QStringLiteral("z"), 0, -1.0e6, 1.0e6, &z)
            || !readTransform(QStringLiteral("rx"), 0, -1.0e6, 1.0e6, &rx)
            || !readTransform(QStringLiteral("ry"), 0, -1.0e6, 1.0e6, &ry)
            || !readTransform(QStringLiteral("rz"), 0, -1.0e6, 1.0e6, &rz)
            || !readTransform(QStringLiteral("scale"), 1, 0.0001, 10000, &scale)
            || !readTransform(QStringLiteral("scaleX"), scale, 0.0001, 10000, &scaleX)
            || !readTransform(QStringLiteral("scaleY"), scale, 0.0001, 10000, &scaleY)
            || !readTransform(QStringLiteral("scaleZ"), scale, 0.0001, 10000, &scaleZ)) {
            return failure(QStringLiteral("Scene object has an invalid transform and cannot be exported."));
        }

        out.name = safeObjName(object.value(QStringLiteral("label")).toString());
        out.position = QVector3D(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        out.rotation = QQuaternion::fromEulerAngles(static_cast<float>(rx),
                                                     static_cast<float>(ry),
                                                     static_cast<float>(rz));
        out.scale = QVector3D(static_cast<float>(scaleX),
                              static_cast<float>(scaleY),
                              static_cast<float>(scaleZ));
        exportObjects.append(std::move(out));
    }

    if (exportObjects.isEmpty())
        return failure(QStringLiteral("No editable mesh objects were available for OBJ export."));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return failure(QStringLiteral("Could not open the OBJ output file: %1").arg(file.errorString()));

    QTextStream output(&file);
    output.setLocale(QLocale::c());
    output << "# QALYVYRON Wavefront OBJ export\n";
    output << "# Mesh coordinates are transformed into scene space.\n";

    qint64 vertexOffset = 0;
    int exportedObjects = 0;
    for (const ExportObject &object : exportObjects) {
        output << "\no " << object.name << "\n";
        for (const QVector3D &localVertex : object.vertices) {
            QVector3D scaled(localVertex.x() * object.scale.x(),
                             localVertex.y() * object.scale.y(),
                             localVertex.z() * object.scale.z());
            const QVector3D worldVertex =
                object.rotation.rotatedVector(scaled) + object.position;
            output << "v " << QString::number(worldVertex.x(), 'g', 9) << ' '
                   << QString::number(worldVertex.y(), 'g', 9) << ' '
                   << QString::number(worldVertex.z(), 'g', 9) << "\n";
        }

        for (const QVector<int> &face : object.faces) {
            output << 'f';
            for (const int index : face)
                output << ' ' << (vertexOffset + static_cast<qint64>(index) + 1);
            output << "\n";
        }

        vertexOffset += object.vertices.size();
        ++exportedObjects;
        output.flush();
        if (file.pos() > kMaximumObjExportBytes) {
            file.cancelWriting();
            return failure(QStringLiteral("OBJ output exceeded the 256 MiB export safety limit."));
        }
        if (output.status() != QTextStream::Ok) {
            file.cancelWriting();
            return failure(QStringLiteral("An I/O error occurred while writing the OBJ file."));
        }
    }

    output.flush();
    if (output.status() != QTextStream::Ok || !file.commit())
        return failure(QStringLiteral("Could not finish writing the OBJ output file."));

    QVariantMap result{
        {QStringLiteral("ok"), true},
        {QStringLiteral("path"), QFileInfo(path).absoluteFilePath()},
        {QStringLiteral("exportedObjects"), exportedObjects},
        {QStringLiteral("skippedObjects"), skippedObjects}
    };
    if (skippedObjects > 0) {
        result.insert(QStringLiteral("warning"),
                      QStringLiteral("%1 hidden or non-editable object(s) were skipped. The OBJ contains visible editable mesh objects only.")
                          .arg(skippedObjects));
    }
    return result;
}


QVariantMap ProjectIO::exportStl(const QUrl &url,
                                 const QVariantMap &document) const
{
    const QString path = localPath(url);
    if (path.isEmpty())
        return failure(QStringLiteral("Choose a local STL output path."));

    const QVariantList objects = document.value(QStringLiteral("objects")).toList();
    if (objects.isEmpty() || objects.size() > kMaximumSceneObjects)
        return failure(QStringLiteral("There are no scene objects to export, or the scene is too large."));

    struct StlTriangle
    {
        QVector3D normal;
        QVector3D a;
        QVector3D b;
        QVector3D c;
    };

    QVector<StlTriangle> triangles;
    int skippedObjects = 0;
    auto validWorldVertex = [](const QVector3D &point) {
        return std::isfinite(point.x()) && std::isfinite(point.y())
            && std::isfinite(point.z())
            && std::abs(point.x()) <= 1.0e6f
            && std::abs(point.y()) <= 1.0e6f
            && std::abs(point.z()) <= 1.0e6f;
    };

    for (const QVariant &rawObject : objects) {
        const QVariantMap object = rawObject.toMap();
        // Keep hidden objects hidden in exported geometry too.
        if (object.contains(QStringLiteral("visible"))
            && !object.value(QStringLiteral("visible")).toBool()) {
            ++skippedObjects;
            continue;
        }
        if (object.value(QStringLiteral("kind")).toString() == QLatin1String("asset")) {
            ++skippedObjects;
            continue; // Runtime-loaded glTF geometry is not embedded in the scene mesh.
        }

        const QVariantMap mesh = object.value(QStringLiteral("mesh")).toMap();
        if (mesh.isEmpty()) {
            ++skippedObjects;
            continue;
        }

        QVector<QVector3D> vertices;
        QVector<QVector<int>> faces;
        QString meshError;
        if (!decodeMesh(mesh, &vertices, &faces, &meshError)) {
            return failure(QStringLiteral("Cannot export %1: %2")
                               .arg(object.value(QStringLiteral("label")).toString(), meshError));
        }

        auto readTransform = [&object](const QString &key, double fallback,
                                       double minimum, double maximum,
                                       double *value) -> bool {
            if (!object.contains(key)) {
                *value = fallback;
                return true;
            }
            bool converted = false;
            const double parsed = object.value(key).toDouble(&converted);
            if (!converted || !std::isfinite(parsed)
                || parsed < minimum || parsed > maximum) {
                return false;
            }
            *value = parsed;
            return true;
        };

        double x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0, scale = 1;
        double scaleX = 1, scaleY = 1, scaleZ = 1;
        if (!readTransform(QStringLiteral("x"), 0, -1.0e6, 1.0e6, &x)
            || !readTransform(QStringLiteral("y"), 0, -1.0e6, 1.0e6, &y)
            || !readTransform(QStringLiteral("z"), 0, -1.0e6, 1.0e6, &z)
            || !readTransform(QStringLiteral("rx"), 0, -1.0e6, 1.0e6, &rx)
            || !readTransform(QStringLiteral("ry"), 0, -1.0e6, 1.0e6, &ry)
            || !readTransform(QStringLiteral("rz"), 0, -1.0e6, 1.0e6, &rz)
            || !readTransform(QStringLiteral("scale"), 1, 0.0001, 10000, &scale)
            || !readTransform(QStringLiteral("scaleX"), scale, 0.0001, 10000, &scaleX)
            || !readTransform(QStringLiteral("scaleY"), scale, 0.0001, 10000, &scaleY)
            || !readTransform(QStringLiteral("scaleZ"), scale, 0.0001, 10000, &scaleZ)) {
            return failure(QStringLiteral("Scene object has an invalid transform and cannot be exported."));
        }

        const QVector3D position(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        const QQuaternion rotation = QQuaternion::fromEulerAngles(static_cast<float>(rx),
                                                                  static_cast<float>(ry),
                                                                  static_cast<float>(rz));
        auto worldVertex = [&](int index) {
            const QVector3D local = vertices.at(index);
            const QVector3D scaled(local.x() * static_cast<float>(scaleX),
                                   local.y() * static_cast<float>(scaleY),
                                   local.z() * static_cast<float>(scaleZ));
            return rotation.rotatedVector(scaled) + position;
        };

        for (const QVector<int> &face : faces) {
            const QVector3D a = worldVertex(face.first());
            if (!validWorldVertex(a))
                return failure(QStringLiteral("World coordinates for '%1' are outside the STL safety range.")
                                   .arg(object.value(QStringLiteral("label")).toString()));

            for (int corner = 1; corner + 1 < face.size(); ++corner) {
                const QVector3D b = worldVertex(face.at(corner));
                const QVector3D c = worldVertex(face.at(corner + 1));
                if (!validWorldVertex(b) || !validWorldVertex(c)) {
                    return failure(QStringLiteral("World coordinates for '%1' are outside the STL safety range.")
                                       .arg(object.value(QStringLiteral("label")).toString()));
                }

                const QVector3D area = QVector3D::crossProduct(b - a, c - a);
                if (!std::isfinite(area.x()) || !std::isfinite(area.y())
                    || !std::isfinite(area.z())) {
                    return failure(QStringLiteral("Mesh '%1' generated invalid triangle coordinates.")
                                       .arg(object.value(QStringLiteral("label")).toString()));
                }
                if (area.lengthSquared() < 1.0e-10f)
                    continue;

                if (triangles.size() >= static_cast<int>(kMaximumStlTriangles)) {
                    return failure(QStringLiteral("STL export exceeds the 100,000-triangle safety limit."));
                }
                triangles.append({area.normalized(), a, b, c});
            }
        }
    }

    if (triangles.isEmpty())
        return failure(QStringLiteral("No visible editable triangles were available for STL export."));

    // Binary STL has a fixed 84-byte header followed by 50 bytes for every triangle.
    const qint64 outputBytes = 84LL + static_cast<qint64>(triangles.size()) * 50LL;
    if (outputBytes > kMaximumStlBytes)
        return failure(QStringLiteral("STL output exceeds the 128 MiB export safety limit."));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return failure(QStringLiteral("Could not open the STL output file: %1").arg(file.errorString()));

    QByteArray header(80, '\0');
    const QByteArray label("QALYVYRON binary STL");
    for (int i = 0; i < label.size(); ++i)
        header[i] = label.at(i);

    QDataStream output(&file);
    output.setByteOrder(QDataStream::LittleEndian);
    output.setFloatingPointPrecision(QDataStream::SinglePrecision);
    if (output.writeRawData(header.constData(), header.size()) != header.size()) {
        file.cancelWriting();
        return failure(QStringLiteral("Could not write the STL header."));
    }

    output << static_cast<quint32>(triangles.size());
    for (const StlTriangle &triangle : triangles) {
        // STL stores one normal and three world-space vertices per triangle.
        output << triangle.normal.x() << triangle.normal.y() << triangle.normal.z()
               << triangle.a.x() << triangle.a.y() << triangle.a.z()
               << triangle.b.x() << triangle.b.y() << triangle.b.z()
               << triangle.c.x() << triangle.c.y() << triangle.c.z()
               << static_cast<quint16>(0);
        if (output.status() != QDataStream::Ok) {
            file.cancelWriting();
            return failure(QStringLiteral("An I/O error occurred while writing the STL output file."));
        }
    }

    if (file.pos() != outputBytes || output.status() != QDataStream::Ok || !file.commit())
        return failure(QStringLiteral("Could not finish writing the binary STL file."));

    QVariantMap result{
        {QStringLiteral("ok"), true},
        {QStringLiteral("path"), QFileInfo(path).absoluteFilePath()},
        {QStringLiteral("exportedTriangles"), triangles.size()},
        {QStringLiteral("skippedObjects"), skippedObjects}
    };
    if (skippedObjects > 0) {
        result.insert(QStringLiteral("warning"),
                      QStringLiteral("%1 hidden or non-editable object(s) were skipped. STL contains visible editable geometry only.")
                          .arg(skippedObjects));
    }
    return result;
}

