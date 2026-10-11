#include "editablegeometry.h"

#include <QByteArray>
#include <QVariantList>
#include <QSet>
#include <QVector2D>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kPrimitiveRadius = 50.0f;
constexpr int kCylinderSegments = 32;
constexpr int kSphereSegments = 32;
constexpr int kSphereLatitudeBands = 16;
constexpr float kEpsilon = 1.0e-5f;
constexpr qint64 kMaximumRenderedVertices = 1000000;

bool finiteVector(const QVector3D &value)
{
    return std::isfinite(value.x()) && std::isfinite(value.y())
        && std::isfinite(value.z());
}
}

EditableGeometry::EditableGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    createPrimitive(m_shape);
}

QString EditableGeometry::shape() const
{
    return m_shape;
}

void EditableGeometry::setShape(const QString &shape)
{
    const QString normalized = shape.trimmed();
    const QString supported = (normalized == QLatin1String("Cube")
                                || normalized == QLatin1String("Sphere")
                                || normalized == QLatin1String("Cylinder")
                                || normalized == QLatin1String("Cone")
                                || normalized == QLatin1String("Plane"))
        ? normalized : QStringLiteral("Cube");

    if (m_shape == supported && !m_vertices.isEmpty())
        return;

    m_shape = supported;
    createPrimitive(supported);
    emit shapeChanged();
    emit meshChanged();
}

int EditableGeometry::vertexCount() const
{
    return m_vertices.size();
}

int EditableGeometry::faceCount() const
{
    return m_faces.size();
}

QVector3D EditableGeometry::vertexPosition(int index) const
{
    if (index < 0 || index >= m_vertices.size())
        return {};
    return m_vertices.at(index);
}

bool EditableGeometry::setVertexPosition(int index, const QVector3D &position)
{
    if (index < 0 || index >= m_vertices.size() || !finiteVector(position)
        || std::abs(position.x()) > 1.0e6f
        || std::abs(position.y()) > 1.0e6f
        || std::abs(position.z()) > 1.0e6f) {
        return false;
    }

    m_vertices[index] = position;
    rebuildGeometry();
    emit meshChanged();
    return true;
}

QVector3D EditableGeometry::centerForFace(const QVector<int> &face) const
{
    QVector3D center;
    if (face.isEmpty())
        return center;

    for (const int index : face)
        center += m_vertices.at(index);

    return center / static_cast<float>(face.size());
}

QVector3D EditableGeometry::normalForFace(const QVector<int> &face) const
{
    // Newell's method also works for non-triangular and mildly non-planar faces.
    QVector3D normal;
    if (face.size() < 3)
        return normal;

    for (int i = 0; i < face.size(); ++i) {
        const QVector3D &a = m_vertices.at(face.at(i));
        const QVector3D &b = m_vertices.at(face.at((i + 1) % face.size()));
        normal.setX(normal.x() + (a.y() - b.y()) * (a.z() + b.z()));
        normal.setY(normal.y() + (a.z() - b.z()) * (a.x() + b.x()));
        normal.setZ(normal.z() + (a.x() - b.x()) * (a.y() + b.y()));
    }

    if (normal.lengthSquared() < kEpsilon * kEpsilon)
        return {};
    return normal.normalized();
}

QVector3D EditableGeometry::faceCenter(int index) const
{
    if (index < 0 || index >= m_faces.size())
        return {};
    return centerForFace(m_faces.at(index));
}

QVector3D EditableGeometry::faceNormal(int index) const
{
    if (index < 0 || index >= m_faces.size())
        return {};
    return normalForFace(m_faces.at(index));
}

bool EditableGeometry::validFace(const QVector<int> &face, int vertexCount) const
{
    if (face.size() < 3 || face.size() > kMaximumFaceCorners)
        return false;

    for (int i = 0; i < face.size(); ++i) {
        const int index = face.at(i);
        if (index < 0 || index >= vertexCount)
            return false;
        if (index == face.at((i + 1) % face.size()))
            return false;
    }
    return true;
}

bool EditableGeometry::extrudeFace(int faceIndex, double distance)
{
    if (faceIndex < 0 || faceIndex >= m_faces.size()
        || !std::isfinite(distance) || std::abs(distance) < 1.0e-4
        || std::abs(distance) > 1.0e6) {
        return false;
    }

    const QVector<int> boundary = m_faces.at(faceIndex);
    const QVector3D normal = normalForFace(boundary);
    if (boundary.size() < 3 || normal.lengthSquared() < kEpsilon * kEpsilon
        || m_vertices.size() + boundary.size() > kMaximumVertices
        || m_faces.size() + boundary.size() > kMaximumFaces) {
        return false;
    }

    qint64 currentRenderedVertices = 0;
    for (const QVector<int> &face : m_faces)
        currentRenderedVertices += static_cast<qint64>(face.size() - 2) * 3;
    const qint64 projectedRenderedVertices =
        currentRenderedVertices + static_cast<qint64>(boundary.size()) * 6;
    if (projectedRenderedVertices > kMaximumRenderedVertices)
        return false;

    QVector<int> extruded;
    extruded.reserve(boundary.size());
    const QVector3D offset = normal * static_cast<float>(distance);
    for (const int oldIndex : boundary) {
        extruded.append(m_vertices.size());
        m_vertices.append(m_vertices.at(oldIndex) + offset);
    }

    // Replace the original cap with the new cap, then bridge its old boundary.
    m_faces[faceIndex] = extruded;
    for (int i = 0; i < boundary.size(); ++i) {
        const int next = (i + 1) % boundary.size();
        QVector<int> side{
            boundary.at(i),
            boundary.at(next),
            extruded.at(next),
            extruded.at(i)
        };
        if (distance < 0)
            std::reverse(side.begin(), side.end());
        m_faces.append(side);
    }

    rebuildGeometry();
    emit meshChanged();
    return true;
}

bool EditableGeometry::insetFace(int faceIndex, double distance)
{
    if (faceIndex < 0 || faceIndex >= m_faces.size()
        || !std::isfinite(distance) || distance < 1.0e-4
        || distance > 1.0e6) {
        return false;
    }

    const QVector<int> boundary = m_faces.at(faceIndex);
    if (boundary.size() < 3
        || m_vertices.size() + boundary.size() > kMaximumVertices
        || m_faces.size() + boundary.size() > kMaximumFaces) {
        return false;
    }

    qint64 currentRenderedVertices = 0;
    for (const QVector<int> &face : m_faces)
        currentRenderedVertices += static_cast<qint64>(face.size() - 2) * 3;
    const qint64 projectedRenderedVertices =
        currentRenderedVertices + static_cast<qint64>(boundary.size()) * 6;
    if (projectedRenderedVertices > kMaximumRenderedVertices)
        return false;

    const QVector3D normal = normalForFace(boundary);
    const QVector3D origin = m_vertices.at(boundary.first());
    const QVector3D firstEdge = m_vertices.at(boundary.at(1)) - origin;
    if (normal.lengthSquared() < kEpsilon * kEpsilon
        || firstEdge.lengthSquared() < kEpsilon * kEpsilon) {
        return false;
    }

    const QVector3D axisU = firstEdge.normalized();
    const QVector3D axisV = QVector3D::crossProduct(normal, axisU).normalized();
    QVector<QVector2D> polygon;
    polygon.reserve(boundary.size());
    for (const int vertexIndex : boundary) {
        const QVector3D relative = m_vertices.at(vertexIndex) - origin;
        if (std::abs(QVector3D::dotProduct(relative, normal)) > 1.0e-2f)
            return false; // A planar inset cannot safely reshape a warped face.
        polygon.append(QVector2D(QVector3D::dotProduct(relative, axisU),
                                 QVector3D::dotProduct(relative, axisV)));
    }

    auto cross2d = [](const QVector2D &a, const QVector2D &b) {
        return a.x() * b.y() - a.y() * b.x();
    };

    double signedAreaTwice = 0.0;
    for (int i = 0; i < polygon.size(); ++i)
        signedAreaTwice += cross2d(polygon.at(i), polygon.at((i + 1) % polygon.size()));

    if (!std::isfinite(signedAreaTwice) || std::abs(signedAreaTwice) < 1.0e-5)
        return false;
    const float winding = signedAreaTwice > 0 ? 1.0f : -1.0f;

    // This first inset operation is intentionally limited to strictly convex
    // planar faces; self-crossing inset loops are rejected rather than saved.
    for (int i = 0; i < polygon.size(); ++i) {
        const QVector2D incoming = polygon.at(i) - polygon.at((i + polygon.size() - 1) % polygon.size());
        const QVector2D outgoing = polygon.at((i + 1) % polygon.size()) - polygon.at(i);
        if (incoming.lengthSquared() < kEpsilon * kEpsilon
            || outgoing.lengthSquared() < kEpsilon * kEpsilon
            || cross2d(incoming, outgoing) * winding <= 1.0e-5f) {
            return false;
        }
    }

    QVector<QVector2D> inwardNormals;
    inwardNormals.reserve(polygon.size());
    for (int i = 0; i < polygon.size(); ++i) {
        const QVector2D edge = polygon.at((i + 1) % polygon.size()) - polygon.at(i);
        QVector2D inward(-edge.y(), edge.x());
        inward.normalize();
        inwardNormals.append(inward * winding);
    }

    QVector<QVector3D> insetPositions;
    insetPositions.reserve(boundary.size());
    QVector<QVector2D> insetPolygon;
    insetPolygon.reserve(boundary.size());

    const float insetDistance = static_cast<float>(distance);
    for (int i = 0; i < polygon.size(); ++i) {
        const int previous = (i + polygon.size() - 1) % polygon.size();
        const QVector2D pointA = polygon.at(i) + inwardNormals.at(previous) * insetDistance;
        const QVector2D pointB = polygon.at(i) + inwardNormals.at(i) * insetDistance;
        const QVector2D directionA = polygon.at(i) - polygon.at(previous);
        const QVector2D directionB = polygon.at((i + 1) % polygon.size()) - polygon.at(i);
        const float denominator = cross2d(directionA, directionB);

        if (std::abs(denominator) < kEpsilon)
            return false;

        const float parameter = cross2d(pointB - pointA, directionB) / denominator;
        const QVector2D insetPoint = pointA + directionA * parameter;
        if (!std::isfinite(insetPoint.x()) || !std::isfinite(insetPoint.y()))
            return false;

        insetPolygon.append(insetPoint);
        const QVector3D position = origin + axisU * insetPoint.x() + axisV * insetPoint.y();
        if (!finiteVector(position)
            || std::abs(position.x()) > 1.0e6f
            || std::abs(position.y()) > 1.0e6f
            || std::abs(position.z()) > 1.0e6f) {
            return false;
        }
        insetPositions.append(position);
    }

    double insetAreaTwice = 0.0;
    for (int i = 0; i < insetPolygon.size(); ++i)
        insetAreaTwice += cross2d(insetPolygon.at(i),
                                  insetPolygon.at((i + 1) % insetPolygon.size()));

    if (!std::isfinite(insetAreaTwice)
        || insetAreaTwice * signedAreaTwice <= 0.0
        || std::abs(insetAreaTwice) >= std::abs(signedAreaTwice)) {
        // Reject a collapsed or crossing inset without modifying the live mesh.
        return false;
    }

    QVector<int> insetIndices;
    insetIndices.reserve(insetPositions.size());
    for (const QVector3D &position : insetPositions) {
        insetIndices.append(m_vertices.size());
        m_vertices.append(position);
    }

    // The outer cap becomes an inner cap. Side faces bridge the old boundary
    // to the inset loop, preserving manifold connectivity for convex faces.
    m_faces[faceIndex] = insetIndices;
    for (int i = 0; i < boundary.size(); ++i) {
        const int next = (i + 1) % boundary.size();
        m_faces.append(QVector<int>{
            boundary.at(i),
            boundary.at(next),
            insetIndices.at(next),
            insetIndices.at(i)
        });
    }

    rebuildGeometry();
    emit meshChanged();
    return true;
}

QVariantMap EditableGeometry::serializeMesh() const
{
    QVariantList vertices;
    vertices.reserve(m_vertices.size());
    for (const QVector3D &vertex : m_vertices) {
        vertices.append(QVariant::fromValue(QVariantList{
            vertex.x(),
            vertex.y(),
            vertex.z()
        }));
    }

    QVariantList faces;
    faces.reserve(m_faces.size());
    for (const QVector<int> &face : m_faces) {
        QVariantList corners;
        corners.reserve(face.size());
        for (const int index : face)
            corners.append(index);
        // QVariantList inherits QList and would flatten this nested list if
        // appended directly; wrap it in QVariant to preserve one face per entry.
        faces.append(QVariant::fromValue(corners));
    }

    return {
        {QStringLiteral("shape"), m_shape},
        {QStringLiteral("vertices"), vertices},
        {QStringLiteral("faces"), faces}
    };
}

bool EditableGeometry::loadMesh(const QVariantMap &data)
{
    const QVariantList rawVertices = data.value(QStringLiteral("vertices")).toList();
    const QVariantList rawFaces = data.value(QStringLiteral("faces")).toList();

    if (rawVertices.size() < 3 || rawVertices.size() > kMaximumVertices
        || rawFaces.isEmpty() || rawFaces.size() > kMaximumFaces) {
        return false;
    }

    QVector<QVector3D> vertices;
    vertices.reserve(rawVertices.size());
    for (const QVariant &rawVertex : rawVertices) {
        const QVariantList components = rawVertex.toList();
        if (components.size() != 3)
            return false;

        double coordinate[3] = {};
        for (int i = 0; i < 3; ++i) {
            bool converted = false;
            coordinate[i] = components.at(i).toDouble(&converted);
            if (!converted || !std::isfinite(coordinate[i])
                || std::abs(coordinate[i]) > 1.0e6)
                return false;
        }

        const QVector3D vertex(static_cast<float>(coordinate[0]),
                               static_cast<float>(coordinate[1]),
                               static_cast<float>(coordinate[2]));
        if (!finiteVector(vertex))
            return false;
        vertices.append(vertex);
    }

    QVector<QVector<int>> faces;
    faces.reserve(rawFaces.size());
    qint64 renderedVertexCount = 0;
    for (const QVariant &rawFace : rawFaces) {
        const QVariantList rawCorners = rawFace.toList();
        if (rawCorners.size() < 3 || rawCorners.size() > kMaximumFaceCorners)
            return false;

        renderedVertexCount += static_cast<qint64>(rawCorners.size() - 2) * 3;
        if (renderedVertexCount > kMaximumRenderedVertices)
            return false;

        QVector<int> face;
        face.reserve(rawCorners.size());
        QSet<int> uniqueCorners;
        for (const QVariant &rawIndex : rawCorners) {
            bool converted = false;
            const int index = rawIndex.toInt(&converted);
            if (!converted || index < 0 || index >= vertices.size()
                || uniqueCorners.contains(index)) {
                return false;
            }
            uniqueCorners.insert(index);
            face.append(index);
        }

        // Validate using the candidate vertex array without mutating the live mesh.
        QVector3D normal;
        for (int i = 0; i < face.size(); ++i) {
            const QVector3D &a = vertices.at(face.at(i));
            const QVector3D &b = vertices.at(face.at((i + 1) % face.size()));
            normal.setX(normal.x() + (a.y() - b.y()) * (a.z() + b.z()));
            normal.setY(normal.y() + (a.z() - b.z()) * (a.x() + b.x()));
            normal.setZ(normal.z() + (a.x() - b.x()) * (a.y() + b.y()));
        }
        if (!finiteVector(normal) || normal.lengthSquared() < kEpsilon * kEpsilon)
            return false;

        faces.append(face);
    }

    const QString requestedShape = data.value(QStringLiteral("shape"), m_shape).toString();
    const QString loadedShape =
        (requestedShape == QLatin1String("Cube")
         || requestedShape == QLatin1String("Sphere")
         || requestedShape == QLatin1String("Cylinder")
         || requestedShape == QLatin1String("Cone")
         || requestedShape == QLatin1String("Plane")
         || requestedShape == QLatin1String("Custom"))
        ? requestedShape : QStringLiteral("Custom");

    m_vertices = vertices;
    m_faces = faces;
    m_shape = loadedShape;
    rebuildGeometry();
    emit shapeChanged();
    emit meshChanged();
    return true;
}

void EditableGeometry::createPrimitive(const QString &shape)
{
    m_vertices.clear();
    m_faces.clear();

    if (shape == QLatin1String("Plane")) {
        m_vertices = {
            {-kPrimitiveRadius, 0, -kPrimitiveRadius},
            { kPrimitiveRadius, 0, -kPrimitiveRadius},
            { kPrimitiveRadius, 0,  kPrimitiveRadius},
            {-kPrimitiveRadius, 0,  kPrimitiveRadius}
        };
        m_faces = {{0, 3, 2, 1}}; // Up-facing plane.
    } else if (shape == QLatin1String("Sphere")) {
        m_vertices.append(QVector3D(0, kPrimitiveRadius, 0));
        QVector<QVector<int>> rings;
        rings.reserve(kSphereLatitudeBands - 1);

        for (int latitude = 1; latitude < kSphereLatitudeBands; ++latitude) {
            const float phi = kPi * latitude / kSphereLatitudeBands;
            QVector<int> ring;
            ring.reserve(kSphereSegments);
            for (int longitude = 0; longitude < kSphereSegments; ++longitude) {
                const float theta = 2.0f * kPi * longitude / kSphereSegments;
                const float sinPhi = std::sin(phi);
                ring.append(m_vertices.size());
                m_vertices.append(QVector3D(
                    kPrimitiveRadius * sinPhi * std::sin(theta),
                    kPrimitiveRadius * std::cos(phi),
                    kPrimitiveRadius * sinPhi * std::cos(theta)));
            }
            rings.append(ring);
        }

        const int southPole = m_vertices.size();
        m_vertices.append(QVector3D(0, -kPrimitiveRadius, 0));

        const QVector<int> &firstRing = rings.first();
        for (int i = 0; i < kSphereSegments; ++i) {
            const int next = (i + 1) % kSphereSegments;
            m_faces.append(QVector<int>{0, firstRing.at(i), firstRing.at(next)});
        }

        for (int ringIndex = 0; ringIndex + 1 < rings.size(); ++ringIndex) {
            const QVector<int> &upper = rings.at(ringIndex);
            const QVector<int> &lower = rings.at(ringIndex + 1);
            for (int i = 0; i < kSphereSegments; ++i) {
                const int next = (i + 1) % kSphereSegments;
                m_faces.append(QVector<int>{
                    upper.at(i), lower.at(i), lower.at(next), upper.at(next)
                });
            }
        }

        const QVector<int> &lastRing = rings.last();
        for (int i = 0; i < kSphereSegments; ++i) {
            const int next = (i + 1) % kSphereSegments;
            m_faces.append(QVector<int>{lastRing.at(i), southPole, lastRing.at(next)});
        }
    } else if (shape == QLatin1String("Cylinder")) {
        for (int ySign : {-1, 1}) {
            for (int i = 0; i < kCylinderSegments; ++i) {
                const float theta = 2.0f * kPi * i / kCylinderSegments;
                m_vertices.append(QVector3D(
                    kPrimitiveRadius * std::sin(theta),
                    ySign * kPrimitiveRadius,
                    kPrimitiveRadius * std::cos(theta)));
            }
        }

        for (int i = 0; i < kCylinderSegments; ++i) {
            const int next = (i + 1) % kCylinderSegments;
            m_faces.append(QVector<int>{i, next,
                                       kCylinderSegments + next,
                                       kCylinderSegments + i});
        }

        QVector<int> bottom;
        QVector<int> top;
        bottom.reserve(kCylinderSegments);
        top.reserve(kCylinderSegments);
        for (int i = 0; i < kCylinderSegments; ++i) {
            bottom.append(i);
            top.prepend(kCylinderSegments + i);
        }
        m_faces.append(bottom);
        m_faces.append(top);
    } else if (shape == QLatin1String("Cone")) {
        for (int i = 0; i < kCylinderSegments; ++i) {
            const float theta = 2.0f * kPi * i / kCylinderSegments;
            m_vertices.append(QVector3D(
                kPrimitiveRadius * std::sin(theta),
                -kPrimitiveRadius,
                kPrimitiveRadius * std::cos(theta)));
        }
        const int tip = m_vertices.size();
        m_vertices.append(QVector3D(0, kPrimitiveRadius, 0));

        for (int i = 0; i < kCylinderSegments; ++i) {
            const int next = (i + 1) % kCylinderSegments;
            m_faces.append(QVector<int>{i, next, tip});
        }

        QVector<int> base;
        base.reserve(kCylinderSegments);
        for (int i = 0; i < kCylinderSegments; ++i)
            base.prepend(i);
        m_faces.append(base);
    } else {
        m_vertices = {
            {-kPrimitiveRadius, -kPrimitiveRadius, -kPrimitiveRadius},
            { kPrimitiveRadius, -kPrimitiveRadius, -kPrimitiveRadius},
            { kPrimitiveRadius,  kPrimitiveRadius, -kPrimitiveRadius},
            {-kPrimitiveRadius,  kPrimitiveRadius, -kPrimitiveRadius},
            {-kPrimitiveRadius, -kPrimitiveRadius,  kPrimitiveRadius},
            { kPrimitiveRadius, -kPrimitiveRadius,  kPrimitiveRadius},
            { kPrimitiveRadius,  kPrimitiveRadius,  kPrimitiveRadius},
            {-kPrimitiveRadius,  kPrimitiveRadius,  kPrimitiveRadius}
        };
        m_faces = {
            {4, 5, 6, 7}, // +Z
            {1, 0, 3, 2}, // -Z
            {5, 1, 2, 6}, // +X
            {0, 4, 7, 3}, // -X
            {3, 7, 6, 2}, // +Y
            {0, 1, 5, 4}  // -Y
        };
    }

    m_shape = shape;
    if (shape != QLatin1String("Plane"))
        orientClosedMeshOutward();
    rebuildGeometry();
}

void EditableGeometry::orientClosedMeshOutward()
{
    for (QVector<int> &face : m_faces) {
        const QVector3D normal = normalForFace(face);
        const QVector3D center = centerForFace(face);
        if (QVector3D::dotProduct(normal, center) < 0)
            std::reverse(face.begin(), face.end());
    }
}

void EditableGeometry::rebuildGeometry()
{
    QByteArray vertexBuffer;
    int renderedVertexCount = 0;
    for (const QVector<int> &face : m_faces) {
        if (face.size() >= 3)
            renderedVertexCount += (face.size() - 2) * 3;
    }

    vertexBuffer.resize(renderedVertexCount * 6 * static_cast<int>(sizeof(float)));
    float *output = reinterpret_cast<float *>(vertexBuffer.data());

    QVector3D minimum;
    QVector3D maximum;
    if (!m_vertices.isEmpty()) {
        minimum = maximum = m_vertices.first();
        for (const QVector3D &vertex : m_vertices) {
            minimum.setX(std::min(minimum.x(), vertex.x()));
            minimum.setY(std::min(minimum.y(), vertex.y()));
            minimum.setZ(std::min(minimum.z(), vertex.z()));
            maximum.setX(std::max(maximum.x(), vertex.x()));
            maximum.setY(std::max(maximum.y(), vertex.y()));
            maximum.setZ(std::max(maximum.z(), vertex.z()));
        }
    }

    for (const QVector<int> &face : m_faces) {
        if (face.size() < 3)
            continue;
        const QVector3D normal = normalForFace(face);
        for (int corner = 1; corner + 1 < face.size(); ++corner) {
            const int triangle[] = {face.first(), face.at(corner), face.at(corner + 1)};
            for (const int vertexIndex : triangle) {
                const QVector3D &position = m_vertices.at(vertexIndex);
                *output++ = position.x();
                *output++ = position.y();
                *output++ = position.z();
                *output++ = normal.x();
                *output++ = normal.y();
                *output++ = normal.z();
            }
        }
    }

    clear();
    setStride(6 * static_cast<int>(sizeof(float)));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0,
                 QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic,
                 3 * static_cast<int>(sizeof(float)),
                 QQuick3DGeometry::Attribute::F32Type);
    setVertexData(vertexBuffer);
    setBounds(minimum, maximum);
    update();
}
