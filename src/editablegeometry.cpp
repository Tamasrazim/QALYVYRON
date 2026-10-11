#include "editablegeometry.h"

#include <QByteArray>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr float kPrimitiveRadius = 50.0f;
constexpr int kCylinderSegments = 32;
constexpr int kSphereSegments = 32;
constexpr int kSphereLatitudeBands = 16;
constexpr float kEpsilon = 1.0e-5f;

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

QVariantMap EditableGeometry::serializeMesh() const
{
    QVariantList vertices;
    vertices.reserve(m_vertices.size());
    for (const QVector3D &vertex : m_vertices) {
        vertices.append(QVariantList{
            vertex.x(),
            vertex.y(),
            vertex.z()
        });
    }

    QVariantList faces;
    faces.reserve(m_faces.size());
    for (const QVector<int> &face : m_faces) {
        QVariantList corners;
        corners.reserve(face.size());
        for (const int index : face)
            corners.append(index);
        faces.append(corners);
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

        const QVector3D vertex(components.at(0).toFloat(),
                               components.at(1).toFloat(),
                               components.at(2).toFloat());
        if (!finiteVector(vertex)
            || std::abs(vertex.x()) > 1.0e6f
            || std::abs(vertex.y()) > 1.0e6f
            || std::abs(vertex.z()) > 1.0e6f) {
            return false;
        }
        vertices.append(vertex);
    }

    QVector<QVector<int>> faces;
    faces.reserve(rawFaces.size());
    for (const QVariant &rawFace : rawFaces) {
        const QVariantList rawCorners = rawFace.toList();
        if (rawCorners.size() < 3 || rawCorners.size() > kMaximumFaceCorners)
            return false;

        QVector<int> face;
        face.reserve(rawCorners.size());
        for (const QVariant &rawIndex : rawCorners)
            face.append(rawIndex.toInt());

        for (const int index : face) {
            if (index < 0 || index >= vertices.size())
                return false;
        }
        for (int i = 0; i < face.size(); ++i) {
            if (face.at(i) == face.at((i + 1) % face.size()))
                return false;
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

    const QString loadedShape = data.value(QStringLiteral("shape"), m_shape).toString();
    m_vertices = vertices;
    m_faces = faces;
    m_shape = loadedShape.isEmpty() ? QStringLiteral("Custom") : loadedShape;
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
            const float phi = static_cast<float>(M_PI) * latitude / kSphereLatitudeBands;
            QVector<int> ring;
            ring.reserve(kSphereSegments);
            for (int longitude = 0; longitude < kSphereSegments; ++longitude) {
                const float theta = 2.0f * static_cast<float>(M_PI) * longitude / kSphereSegments;
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
                const float theta = 2.0f * static_cast<float>(M_PI) * i / kCylinderSegments;
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
            const float theta = 2.0f * static_cast<float>(M_PI) * i / kCylinderSegments;
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
}
