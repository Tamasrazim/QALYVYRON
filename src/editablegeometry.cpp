// What this does: Implements the scene-editing operations: it changes geometry and keeps the data used by the editor in step.

#include "editablegeometry.h"

#include <QByteArray>
#include <QHash>
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
constexpr int kTorusMajorSegments = 32;
constexpr int kTorusMinorSegments = 12;
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
                                || normalized == QLatin1String("Plane")
                                || normalized == QLatin1String("Torus"))
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

int EditableGeometry::edgeCount() const
{
    return m_edges.size();
}

QVariantList EditableGeometry::edgeVertexIndices(int index) const
{
    if (index < 0 || index >= m_edges.size())
        return {};

    const QPair<int, int> edge = m_edges.at(index);
    return {edge.first, edge.second};
}

QVector3D EditableGeometry::edgeCenter(int index) const
{
    if (index < 0 || index >= m_edges.size())
        return {};

    const QPair<int, int> edge = m_edges.at(index);
    return (m_vertices.at(edge.first) + m_vertices.at(edge.second)) * 0.5f;
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

    QVector2D polygonCentroid;
    for (const QVector2D &point : polygon)
        polygonCentroid += point;
    polygonCentroid /= static_cast<float>(polygon.size());

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

        // In an oversized inset, offset lines can cross and form a small loop
        // whose area appears valid despite its corners having swapped sides.
        // Every inset corner must remain on the same side of the face centroid.
        const QVector2D originalRadius = polygon.at(i) - polygonCentroid;
        const QVector2D insetRadius = insetPoint - polygonCentroid;
        if (QVector2D::dotProduct(originalRadius, insetRadius) <= 1.0e-5f)
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

bool EditableGeometry::deleteFace(int faceIndex)
{
    // Leave at least one face so the object remains a usable mesh.
    if (faceIndex < 0 || faceIndex >= m_faces.size() || m_faces.size() <= 1)
        return false;

    QVector<QVector<int>> remainingFaces = m_faces;
    remainingFaces.removeAt(faceIndex);

    QVector<bool> used(m_vertices.size(), false);
    for (const QVector<int> &face : remainingFaces) {
        for (const int index : face) {
            if (index < 0 || index >= m_vertices.size())
                return false;
            used[index] = true;
        }
    }

    // Compact unused vertices and remap all surviving faces before committing.
    // Constructing this candidate first means malformed input cannot partially
    // mutate the mesh if the operation is rejected.
    QVector<int> remap(m_vertices.size(), -1);
    QVector<QVector3D> compactVertices;
    compactVertices.reserve(m_vertices.size());
    for (int oldIndex = 0; oldIndex < m_vertices.size(); ++oldIndex) {
        if (!used.at(oldIndex))
            continue;
        remap[oldIndex] = compactVertices.size();
        compactVertices.append(m_vertices.at(oldIndex));
    }

    for (QVector<int> &face : remainingFaces) {
        for (int &index : face) {
            index = remap.at(index);
            if (index < 0)
                return false;
        }
    }

    if (remainingFaces.isEmpty() || compactVertices.size() < 3)
        return false;

    m_vertices = compactVertices;
    m_faces = remainingFaces;
    rebuildGeometry();
    emit meshChanged();
    return true;
}

bool EditableGeometry::mergeVertices(int sourceIndex, int targetIndex)
{
    if (sourceIndex < 0 || sourceIndex >= m_vertices.size()
        || targetIndex < 0 || targetIndex >= m_vertices.size()
        || sourceIndex == targetIndex) {
        return false;
    }

    QVector<QVector3D> candidateVertices = m_vertices;
    candidateVertices[sourceIndex] = candidateVertices.at(targetIndex);

    QVector<QVector<int>> candidateFaces;
    candidateFaces.reserve(m_faces.size());
    for (const QVector<int> &originalFace : m_faces) {
        QVector<int> face;
        face.reserve(originalFace.size());

        for (const int originalIndex : originalFace) {
            if (originalIndex < 0 || originalIndex >= candidateVertices.size())
                return false;

            const int index = originalIndex == sourceIndex ? targetIndex : originalIndex;
            if (face.isEmpty() || face.last() != index)
                face.append(index);
        }

        // A closing duplicate is also a zero-length edge after the merge.
        if (face.size() > 1 && face.first() == face.last())
            face.removeLast();

        if (face.size() < 3)
            continue;

        QSet<int> uniqueIndices;
        for (const int index : face)
            uniqueIndices.insert(index);
        // Non-consecutive repeated indices would make the polygon self-touch.
        // Drop that face rather than retaining invalid topology.
        if (uniqueIndices.size() != face.size())
            continue;

        QVector3D normal;
        for (int i = 0; i < face.size(); ++i) {
            const QVector3D &a = candidateVertices.at(face.at(i));
            const QVector3D &b = candidateVertices.at(face.at((i + 1) % face.size()));
            normal.setX(normal.x() + (a.y() - b.y()) * (a.z() + b.z()));
            normal.setY(normal.y() + (a.z() - b.z()) * (a.x() + b.x()));
            normal.setZ(normal.z() + (a.x() - b.x()) * (a.y() + b.y()));
        }
        if (!finiteVector(normal) || normal.lengthSquared() < kEpsilon * kEpsilon)
            continue;

        candidateFaces.append(face);
    }

    // Refuse a merge that destroys the complete surface.
    if (candidateFaces.isEmpty())
        return false;

    QVector<bool> used(candidateVertices.size(), false);
    for (const QVector<int> &face : candidateFaces) {
        for (const int index : face)
            used[index] = true;
    }

    QVector<int> remap(candidateVertices.size(), -1);
    QVector<QVector3D> compactVertices;
    compactVertices.reserve(candidateVertices.size());
    for (int oldIndex = 0; oldIndex < candidateVertices.size(); ++oldIndex) {
        if (!used.at(oldIndex))
            continue;
        remap[oldIndex] = compactVertices.size();
        compactVertices.append(candidateVertices.at(oldIndex));
    }

    for (QVector<int> &face : candidateFaces) {
        for (int &index : face) {
            index = remap.at(index);
            if (index < 0)
                return false;
        }
    }

    if (compactVertices.size() < 3 || candidateFaces.isEmpty())
        return false;

    m_vertices = compactVertices;
    m_faces = candidateFaces;
    rebuildGeometry();
    emit meshChanged();
    return true;
}

bool EditableGeometry::createFace(const QVariantList &vertexIndices)
{
    const int cornerCount = vertexIndices.size();
    if (cornerCount < 3 || cornerCount > kMaximumFaceCorners
        || m_faces.size() >= kMaximumFaces) {
        return false;
    }

    QVector<int> face;
    face.reserve(cornerCount);
    QSet<int> unique;
    for (const QVariant &rawIndex : vertexIndices) {
        bool converted = false;
        const int index = rawIndex.toInt(&converted);
        if (!converted || index < 0 || index >= m_vertices.size()
            || unique.contains(index)) {
            return false;
        }
        unique.insert(index);
        face.append(index);
    }

    qint64 currentRenderedVertices = 0;
    for (const QVector<int> &existing : m_faces)
        currentRenderedVertices += static_cast<qint64>(existing.size() - 2) * 3;
    currentRenderedVertices += static_cast<qint64>(cornerCount - 2) * 3;
    if (currentRenderedVertices > kMaximumRenderedVertices)
        return false;

    const QVector3D normal = normalForFace(face);
    if (!finiteVector(normal) || normal.lengthSquared() < kEpsilon * kEpsilon)
        return false;

    const QVector3D origin = m_vertices.at(face.first());
    const QVector3D firstEdge = m_vertices.at(face.at(1)) - origin;
    if (firstEdge.lengthSquared() < kEpsilon * kEpsilon)
        return false;
    const QVector3D axisU = firstEdge.normalized();
    const QVector3D axisV = QVector3D::crossProduct(normal, axisU).normalized();

    QVector<QVector2D> polygon;
    polygon.reserve(cornerCount);
    float coordinateExtent = 1.0f;
    for (const int index : face) {
        const QVector3D relative = m_vertices.at(index) - origin;
        if (std::abs(QVector3D::dotProduct(relative, normal)) > 1.0e-2f)
            return false; // This operation only adds planar polygon faces.

        coordinateExtent = std::max(coordinateExtent, relative.length());
        polygon.append(QVector2D(QVector3D::dotProduct(relative, axisU),
                                 QVector3D::dotProduct(relative, axisV)));
    }

    const float epsilon = std::max(1.0e-5f, coordinateExtent * 1.0e-6f);
    auto cross2d = [](const QVector2D &a, const QVector2D &b) {
        return a.x() * b.y() - a.y() * b.x();
    };
    auto orientation = [&cross2d](const QVector2D &a,
                                  const QVector2D &b,
                                  const QVector2D &c) {
        return cross2d(b - a, c - a);
    };

    double signedAreaTwice = 0.0;
    for (int i = 0; i < cornerCount; ++i)
        signedAreaTwice += cross2d(polygon.at(i), polygon.at((i + 1) % cornerCount));
    if (!std::isfinite(signedAreaTwice) || std::abs(signedAreaTwice) <= epsilon)
        return false;

    // The current renderer tessellates polygons as a triangle fan, so accept
    // only strictly convex, planar faces here. Rejecting unsupported topology
    // is safer than silently producing a visibly incorrect surface.
    const float winding = signedAreaTwice > 0 ? 1.0f : -1.0f;
    for (int i = 0; i < cornerCount; ++i) {
        const QVector2D &previous = polygon.at((i + cornerCount - 1) % cornerCount);
        const QVector2D &current = polygon.at(i);
        const QVector2D &next = polygon.at((i + 1) % cornerCount);
        if (orientation(previous, current, next) * winding <= epsilon)
            return false;
    }

    auto onSegment = [&epsilon](const QVector2D &a,
                                const QVector2D &b,
                                const QVector2D &p) {
        return p.x() >= std::min(a.x(), b.x()) - epsilon
            && p.x() <= std::max(a.x(), b.x()) + epsilon
            && p.y() >= std::min(a.y(), b.y()) - epsilon
            && p.y() <= std::max(a.y(), b.y()) + epsilon;
    };
    auto segmentsIntersect = [&orientation, &onSegment, &epsilon](
                                 const QVector2D &a, const QVector2D &b,
                                 const QVector2D &c, const QVector2D &d) {
        const float o1 = orientation(a, b, c);
        const float o2 = orientation(a, b, d);
        const float o3 = orientation(c, d, a);
        const float o4 = orientation(c, d, b);

        if (((o1 > epsilon && o2 < -epsilon) || (o1 < -epsilon && o2 > epsilon))
            && ((o3 > epsilon && o4 < -epsilon) || (o3 < -epsilon && o4 > epsilon)))
            return true;
        if (std::abs(o1) <= epsilon && onSegment(a, b, c)) return true;
        if (std::abs(o2) <= epsilon && onSegment(a, b, d)) return true;
        if (std::abs(o3) <= epsilon && onSegment(c, d, a)) return true;
        if (std::abs(o4) <= epsilon && onSegment(c, d, b)) return true;
        return false;
    };

    // Non-adjacent edges may not cross or touch.
    for (int i = 0; i < cornerCount; ++i) {
        const int iNext = (i + 1) % cornerCount;
        for (int j = i + 1; j < cornerCount; ++j) {
            const int jNext = (j + 1) % cornerCount;
            if (i == j || iNext == j || jNext == i)
                continue;
            if (segmentsIntersect(polygon.at(i), polygon.at(iNext),
                                  polygon.at(j), polygon.at(jNext))) {
                return false;
            }
        }
    }

    m_faces.append(face);
    rebuildGeometry();
    emit meshChanged();
    return true;
}

bool EditableGeometry::createFaceFromEdges(const QVariantList &edgeIndices)
{
    if (edgeIndices.size() < 3 || edgeIndices.size() > kMaximumFaceCorners)
        return false;

    QVector<QPair<int, int>> selectedEdges;
    selectedEdges.reserve(edgeIndices.size());
    QSet<int> uniqueEdgeIndices;
    QHash<int, QVector<int>> adjacency;

    for (const QVariant &rawIndex : edgeIndices) {
        bool converted = false;
        const int edgeIndex = rawIndex.toInt(&converted);
        if (!converted || edgeIndex < 0 || edgeIndex >= m_edges.size()
            || uniqueEdgeIndices.contains(edgeIndex)) {
            return false;
        }

        uniqueEdgeIndices.insert(edgeIndex);
        const QPair<int, int> edge = m_edges.at(edgeIndex);
        if (edge.first < 0 || edge.second < 0
            || edge.first >= m_vertices.size() || edge.second >= m_vertices.size()
            || edge.first == edge.second) {
            return false;
        }

        selectedEdges.append(edge);
        adjacency[edge.first].append(edge.second);
        adjacency[edge.second].append(edge.first);
    }

    // A simple boundary loop has exactly two selected neighbours at each
    // vertex. This rejects open chains and branched selections before mutation.
    for (auto it = adjacency.cbegin(); it != adjacency.cend(); ++it) {
        if (it.value().size() != 2)
            return false;
    }

    const int start = selectedEdges.first().first;
    int previous = -1;
    int current = start;
    QVector<int> orderedVertices;
    orderedVertices.reserve(selectedEdges.size());

    while (true) {
        if (orderedVertices.contains(current))
            return false;

        orderedVertices.append(current);
        const auto it = adjacency.constFind(current);
        if (it == adjacency.cend() || it.value().size() != 2)
            return false;

        const QVector<int> &neighbors = it.value();
        const int next = neighbors.at(0) == previous ? neighbors.at(1) : neighbors.at(0);
        previous = current;
        current = next;

        if (current == start)
            break;
        if (orderedVertices.size() >= selectedEdges.size())
            return false;
    }

    // Degree checks alone allow multiple disconnected loops. The traversal
    // must account for every selected edge in the same simple cycle.
    if (orderedVertices.size() != selectedEdges.size())
        return false;

    // When a loop borders an open boundary (for example, a deleted face),
    // orient the new cap opposite to the directed boundary of its existing
    // neighbour faces. The user may select edges in any order, so do not let
    // click order arbitrarily flip the resulting face normal.
    const auto edgeKey = [](int a, int b) {
        const int low = std::min(a, b);
        const int high = std::max(a, b);
        return (static_cast<quint64>(static_cast<quint32>(low)) << 32)
            | static_cast<quint32>(high);
    };
    QHash<quint64, QVector<QPair<int, int>>> edgeUses;
    for (const QVector<int> &face : m_faces) {
        if (face.size() < 2)
            continue;
        for (int i = 0; i < face.size(); ++i) {
            const int a = face.at(i);
            const int b = face.at((i + 1) % face.size());
            if (a < 0 || b < 0 || a >= m_vertices.size()
                || b >= m_vertices.size() || a == b) {
                continue;
            }
            edgeUses[edgeKey(a, b)].append(qMakePair(a, b));
        }
    }

    int orientationAgreements = 0;
    int orientationConflicts = 0;
    for (int i = 0; i < orderedVertices.size(); ++i) {
        const int a = orderedVertices.at(i);
        const int b = orderedVertices.at((i + 1) % orderedVertices.size());
        const auto uses = edgeUses.value(edgeKey(a, b));
        if (uses.size() != 1)
            continue;

        const QPair<int, int> adjacentDirection = uses.first();
        if (a == adjacentDirection.second && b == adjacentDirection.first)
            ++orientationAgreements; // Cap edge opposes its existing neighbour.
        else if (a == adjacentDirection.first && b == adjacentDirection.second)
            ++orientationConflicts;
    }

    if (orientationAgreements > 0 && orientationConflicts > 0)
        return false; // Inconsistent boundary winding is not safe to fill.
    if (orientationConflicts > 0)
        std::reverse(orderedVertices.begin(), orderedVertices.end());

    QVariantList orderedIndices;
    orderedIndices.reserve(orderedVertices.size());
    for (const int vertexIndex : orderedVertices)
        orderedIndices.append(vertexIndex);

    // createFace owns the coplanarity, convexity, self-intersection, safety,
    // mesh rebuild and notification checks. No topology changes occur before it.
    return createFace(orderedIndices);
}

bool EditableGeometry::bevelEdge(int edgeIndex, double distance)
{
    if (edgeIndex < 0 || edgeIndex >= m_edges.size()
        || m_vertices.size() + 4 > kMaximumVertices
        || m_faces.size() + 1 > kMaximumFaces
        || !std::isfinite(distance) || distance <= kEpsilon) {
        return false;
    }

    const QPair<int, int> edge = m_edges.at(edgeIndex);
    const int first = edge.first;
    const int second = edge.second;
    if (first < 0 || second < 0 || first >= m_vertices.size()
        || second >= m_vertices.size() || first == second) {
        return false;
    }

    const auto edgeKey = [](int a, int b) {
        const int low = std::min(a, b);
        const int high = std::max(a, b);
        return (static_cast<quint64>(static_cast<quint32>(low)) << 32)
            | static_cast<quint32>(high);
    };

    // Keep this first bevel tool predictable: both ends must be ordinary,
    // closed three-edge corners, like the corners of a cube.
    QHash<int, QSet<int>> vertexNeighbors;
    for (const QPair<int, int> &indexedEdge : m_edges) {
        vertexNeighbors[indexedEdge.first].insert(indexedEdge.second);
        vertexNeighbors[indexedEdge.second].insert(indexedEdge.first);
    }
    if (vertexNeighbors.value(first).size() != 3
        || vertexNeighbors.value(second).size() != 3) {
        return false;
    }

    struct IncidentFace
    {
        int faceIndex = -1;
        int firstNeighbor = -1;
        int secondNeighbor = -1;
        bool firstToSecond = false;
        int firstPoint = -1;
        int secondPoint = -1;
    };

    QVector<IncidentFace> incidentFaces;
    int firstFaceCount = 0;
    int secondFaceCount = 0;
    for (int faceIndex = 0; faceIndex < m_faces.size(); ++faceIndex) {
        const QVector<int> &face = m_faces.at(faceIndex);
        if (face.count(first) > 1 || face.count(second) > 1)
            return false;
        if (face.contains(first))
            ++firstFaceCount;
        if (face.contains(second))
            ++secondFaceCount;
        if (face.size() < 3)
            return false;

        for (int i = 0; i < face.size(); ++i) {
            const int current = face.at(i);
            const int next = face.at((i + 1) % face.size());
            if (!((current == first && next == second)
                  || (current == second && next == first))) {
                continue;
            }

            IncidentFace info;
            info.faceIndex = faceIndex;
            info.firstToSecond = current == first;
            if (info.firstToSecond) {
                info.firstNeighbor = face.at((i + face.size() - 1) % face.size());
                info.secondNeighbor = face.at((i + 2) % face.size());
            } else {
                info.secondNeighbor = face.at((i + face.size() - 1) % face.size());
                info.firstNeighbor = face.at((i + 2) % face.size());
            }
            incidentFaces.append(info);
        }
    }

    // Open edges or inconsistent face winding cannot produce a closed bevel.
    if (incidentFaces.size() != 2 || firstFaceCount != 3 || secondFaceCount != 3
        || incidentFaces.at(0).faceIndex == incidentFaces.at(1).faceIndex
        || incidentFaces.at(0).firstToSecond == incidentFaces.at(1).firstToSecond
        || incidentFaces.at(0).firstNeighbor == incidentFaces.at(1).firstNeighbor
        || incidentFaces.at(0).secondNeighbor == incidentFaces.at(1).secondNeighbor) {
        return false;
    }

    QHash<quint64, int> bevelPointByEdge;
    QVector<QVector3D> candidateVertices = m_vertices;

    auto createBevelPoint = [&](int endpoint, int neighbor) -> int {
        const quint64 key = edgeKey(endpoint, neighbor);
        const auto existing = bevelPointByEdge.constFind(key);
        if (existing != bevelPointByEdge.cend())
            return existing.value();
        if (neighbor < 0 || neighbor >= m_vertices.size() || neighbor == endpoint)
            return -1;

        const QVector3D start = m_vertices.at(endpoint);
        const QVector3D end = m_vertices.at(neighbor);
        const QVector3D direction = end - start;
        const float edgeLength = direction.length();
        // Leave enough room beyond the bevel so adjacent edges do not collapse.
        if (!std::isfinite(edgeLength) || edgeLength <= 2.0 * distance)
            return -1;

        const QVector3D point = start + direction * static_cast<float>(distance / edgeLength);
        if (!finiteVector(point) || std::abs(point.x()) > 1.0e6f
            || std::abs(point.y()) > 1.0e6f || std::abs(point.z()) > 1.0e6f) {
            return -1;
        }

        const int index = candidateVertices.size();
        candidateVertices.append(point);
        bevelPointByEdge.insert(key, index);
        return index;
    };

    for (IncidentFace &info : incidentFaces) {
        info.firstPoint = createBevelPoint(first, info.firstNeighbor);
        info.secondPoint = createBevelPoint(second, info.secondNeighbor);
        if (info.firstPoint < 0 || info.secondPoint < 0)
            return false;
    }
    if (candidateVertices.size() != m_vertices.size() + 4)
        return false;

    QVector<QVector<int>> candidateFaces;
    candidateFaces.reserve(m_faces.size() + 1);
    for (int faceIndex = 0; faceIndex < m_faces.size(); ++faceIndex) {
        const QVector<int> &face = m_faces.at(faceIndex);
        QVector<int> candidate;
        candidate.reserve(face.size() + 2);

        const IncidentFace *incident = nullptr;
        for (const IncidentFace &info : incidentFaces) {
            if (info.faceIndex == faceIndex) {
                incident = &info;
                break;
            }
        }

        if (incident) {
            // The two faces beside the chosen edge keep one new edge segment each.
            for (const int vertex : face) {
                if (vertex == first)
                    candidate.append(incident->firstPoint);
                else if (vertex == second)
                    candidate.append(incident->secondPoint);
                else
                    candidate.append(vertex);
            }
        } else {
            // Faces at either endpoint get two points in boundary order, trimming
            // the old corner while keeping the shared edges connected.
            for (int i = 0; i < face.size(); ++i) {
                const int vertex = face.at(i);
                if (vertex != first && vertex != second) {
                    candidate.append(vertex);
                    continue;
                }

                const int previous = face.at((i + face.size() - 1) % face.size());
                const int next = face.at((i + 1) % face.size());
                const auto previousPoint = bevelPointByEdge.constFind(edgeKey(vertex, previous));
                const auto nextPoint = bevelPointByEdge.constFind(edgeKey(vertex, next));
                if (previousPoint == bevelPointByEdge.cend()
                    || nextPoint == bevelPointByEdge.cend()) {
                    return false;
                }
                candidate.append(previousPoint.value());
                candidate.append(nextPoint.value());
            }
        }

        if (candidate.size() < 3 || candidate.size() > kMaximumFaceCorners
            || !validFace(candidate, candidateVertices.size())) {
            return false;
        }
        candidateFaces.append(candidate);
    }

    // Build the new flat face with winding opposite to the two faces being cut.
    const IncidentFace &firstIncident = incidentFaces.first();
    const IncidentFace &secondIncident = incidentFaces.last();
    QVector<int> bevelFace;
    if (firstIncident.firstToSecond) {
        bevelFace = {firstIncident.secondPoint, firstIncident.firstPoint,
                     secondIncident.firstPoint, secondIncident.secondPoint};
    } else {
        bevelFace = {firstIncident.firstPoint, firstIncident.secondPoint,
                     secondIncident.secondPoint, secondIncident.firstPoint};
    }
    if (!validFace(bevelFace, candidateVertices.size()))
        return false;

    QVector3D bevelNormal;
    for (int i = 0; i < bevelFace.size(); ++i) {
        const QVector3D &a = candidateVertices.at(bevelFace.at(i));
        const QVector3D &b = candidateVertices.at(bevelFace.at((i + 1) % bevelFace.size()));
        bevelNormal.setX(bevelNormal.x() + (a.y() - b.y()) * (a.z() + b.z()));
        bevelNormal.setY(bevelNormal.y() + (a.z() - b.z()) * (a.x() + b.x()));
        bevelNormal.setZ(bevelNormal.z() + (a.x() - b.x()) * (a.y() + b.y()));
    }
    if (!finiteVector(bevelNormal) || bevelNormal.lengthSquared() < kEpsilon * kEpsilon)
        return false;

    const QVector3D normalizedNormal = bevelNormal.normalized();
    const QVector3D origin = candidateVertices.at(bevelFace.first());
    float extent = 1.0f;
    for (const int index : bevelFace)
        extent = std::max(extent, (candidateVertices.at(index) - origin).length());
    const float planeTolerance = std::max(1.0e-3f, extent * 1.0e-3f);
    for (const int index : bevelFace) {
        if (std::abs(QVector3D::dotProduct(candidateVertices.at(index) - origin,
                                           normalizedNormal)) > planeTolerance) {
            return false;
        }
    }

    // The renderer fans each polygon into triangles, so do not accept a folded quad.
    for (int i = 0; i < bevelFace.size(); ++i) {
        const QVector3D &a = candidateVertices.at(bevelFace.at(i));
        const QVector3D &b = candidateVertices.at(bevelFace.at((i + 1) % bevelFace.size()));
        const QVector3D &c = candidateVertices.at(bevelFace.at((i + 2) % bevelFace.size()));
        const float turn = QVector3D::dotProduct(
            QVector3D::crossProduct(b - a, c - b), normalizedNormal);
        if (!std::isfinite(turn) || turn <= kEpsilon)
            return false;
    }
    candidateFaces.append(bevelFace);

    qint64 renderedVertices = 0;
    QHash<quint64, int> edgeUseCounts;
    for (const QVector<int> &face : candidateFaces) {
        QVector3D normal;
        for (int i = 0; i < face.size(); ++i) {
            const QVector3D &a = candidateVertices.at(face.at(i));
            const QVector3D &b = candidateVertices.at(face.at((i + 1) % face.size()));
            normal.setX(normal.x() + (a.y() - b.y()) * (a.z() + b.z()));
            normal.setY(normal.y() + (a.z() - b.z()) * (a.x() + b.x()));
            normal.setZ(normal.z() + (a.x() - b.x()) * (a.y() + b.y()));

            const quint64 key = edgeKey(face.at(i), face.at((i + 1) % face.size()));
            const int uses = edgeUseCounts.value(key) + 1;
            if (uses > 2)
                return false;
            edgeUseCounts.insert(key, uses);
        }
        if (!finiteVector(normal) || normal.lengthSquared() < kEpsilon * kEpsilon)
            return false;

        renderedVertices += static_cast<qint64>(face.size() - 2) * 3;
        if (renderedVertices > kMaximumRenderedVertices)
            return false;
    }

    // A closed bevel keeps every edge shared by exactly two faces.
    for (auto it = edgeUseCounts.cbegin(); it != edgeUseCounts.cend(); ++it) {
        if (it.value() != 2)
            return false;
    }

    m_vertices = candidateVertices;
    m_faces = candidateFaces;
    rebuildGeometry();
    emit meshChanged();
    return true;
}

bool EditableGeometry::subdivideEdge(int edgeIndex)
{
    if (edgeIndex < 0 || edgeIndex >= m_edges.size()
        || m_vertices.size() >= kMaximumVertices) {
        return false;
    }

    const QPair<int, int> edge = m_edges.at(edgeIndex);
    const int first = edge.first;
    const int second = edge.second;
    if (first < 0 || second < 0 || first >= m_vertices.size()
        || second >= m_vertices.size() || first == second) {
        return false;
    }

    const QVector3D midpoint = (m_vertices.at(first) + m_vertices.at(second)) * 0.5f;
    if (!finiteVector(midpoint) || std::abs(midpoint.x()) > 1.0e6f
        || std::abs(midpoint.y()) > 1.0e6f || std::abs(midpoint.z()) > 1.0e6f) {
        return false;
    }

    const int midpointIndex = m_vertices.size();
    QVector<QVector<int>> candidateFaces;
    candidateFaces.reserve(m_faces.size());
    bool foundAdjacentFace = false;
    qint64 projectedRenderedVertices = 0;

    // Build a candidate topology first. A valid edge gets one midpoint in
    // every incident face boundary, preserving the surrounding surfaces.
    for (const QVector<int> &face : m_faces) {
        if (face.size() < 3)
            return false;

        QVector<int> candidate;
        candidate.reserve(face.size() + 1);
        for (int i = 0; i < face.size(); ++i) {
            const int current = face.at(i);
            const int next = face.at((i + 1) % face.size());
            candidate.append(current);
            if ((current == first && next == second)
                || (current == second && next == first)) {
                candidate.append(midpointIndex);
                foundAdjacentFace = true;
            }
        }

        if (candidate.size() > kMaximumFaceCorners)
            return false;
        projectedRenderedVertices += static_cast<qint64>(candidate.size() - 2) * 3;
        if (projectedRenderedVertices > kMaximumRenderedVertices)
            return false;
        candidateFaces.append(candidate);
    }

    if (!foundAdjacentFace)
        return false;

    // Commit only after the entire candidate has passed the limits above.
    m_vertices.append(midpoint);
    m_faces = candidateFaces;
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
         || requestedShape == QLatin1String("Torus")
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
    } else if (shape == QLatin1String("Torus")) {
        // Build a real ring mesh. The major circle makes the ring; each minor
        // circle supplies the tube thickness. Shared vertices keep it editable.
        const float majorRadius = kPrimitiveRadius * 0.68f;
        const float tubeRadius = kPrimitiveRadius * 0.32f;
        QVector<QVector<int>> rings;
        rings.reserve(kTorusMajorSegments);

        for (int major = 0; major < kTorusMajorSegments; ++major) {
            const float theta = 2.0f * kPi * major / kTorusMajorSegments;
            const float aroundX = std::cos(theta);
            const float aroundZ = std::sin(theta);
            QVector<int> ring;
            ring.reserve(kTorusMinorSegments);

            for (int minor = 0; minor < kTorusMinorSegments; ++minor) {
                const float phi = 2.0f * kPi * minor / kTorusMinorSegments;
                const float tubeOffset = majorRadius + tubeRadius * std::cos(phi);
                ring.append(m_vertices.size());
                m_vertices.append(QVector3D(
                    tubeOffset * aroundX,
                    tubeRadius * std::sin(phi),
                    tubeOffset * aroundZ));
            }
            rings.append(ring);
        }

        for (int major = 0; major < kTorusMajorSegments; ++major) {
            const int nextMajor = (major + 1) % kTorusMajorSegments;
            const QVector<int> &ring = rings.at(major);
            const QVector<int> &nextRing = rings.at(nextMajor);
            for (int minor = 0; minor < kTorusMinorSegments; ++minor) {
                const int nextMinor = (minor + 1) % kTorusMinorSegments;
                // This winding points normals outward from the tube, even along the inner wall.
                m_faces.append(QVector<int>{
                    ring.at(minor),
                    ring.at(nextMinor),
                    nextRing.at(nextMinor),
                    nextRing.at(minor)
                });
            }
        }
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
    // The torus already has the correct winding. The convex-mesh helper below
    // would incorrectly flip faces along the inside of the ring.
    if (shape != QLatin1String("Plane") && shape != QLatin1String("Torus"))
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

void EditableGeometry::rebuildEdgeIndex()
{
    m_edges.clear();
    QSet<quint64> seen;

    // Walk face boundaries in index order and canonicalize endpoint pairs.
    // Shared edges deduplicate even when adjacent faces traverse them oppositely.
    for (const QVector<int> &face : m_faces) {
        if (face.size() < 2)
            continue;

        for (int i = 0; i < face.size(); ++i) {
            const int first = face.at(i);
            const int second = face.at((i + 1) % face.size());
            if (first < 0 || second < 0 || first >= m_vertices.size()
                || second >= m_vertices.size() || first == second) {
                continue;
            }

            const int a = std::min(first, second);
            const int b = std::max(first, second);
            const quint64 key = (static_cast<quint64>(static_cast<quint32>(a)) << 32)
                | static_cast<quint32>(b);
            if (seen.contains(key))
                continue;

            seen.insert(key);
            m_edges.append(qMakePair(a, b));
        }
    }
}

void EditableGeometry::rebuildGeometry()
{
    rebuildEdgeIndex();

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
