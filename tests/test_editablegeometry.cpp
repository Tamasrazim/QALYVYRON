// What this does: Checks that the geometry-editing functions change scene objects as expected and handle invalid input safely.

#include "editablegeometry.h"

#include <QtTest>
#include <QSet>

#include <limits>

class EditableGeometryTests final : public QObject
{
    Q_OBJECT

private slots:
    void cubeHasEditableTopology();
    void edgeIndexExposesUniqueEdgesAndCenters();
    void edgeIndexTracksTopologyChanges();
    void subdividingEdgeAddsOneMidpointAndPreservesFaces();
    void invalidEdgeSubdivisionIsAtomic();
    void bevelsCubeEdgeAndKeepsClosedTopology();
    void invalidEdgeBevelIsAtomic();
    void closedEdgeLoopCreatesFaceForDeletedSurface();
    void invalidEdgeLoopsAreRejectedAtomically();
    void primitiveShapesHaveExpectedTopology();
    void torusHasClosedEditableTopology();
    void smoothShadingStateIsSavedAndRestored();
    void vertexEditsChangeSerializedMesh();
    void invalidVertexEditsAreRejectedAtomically();
    void extrudingFaceAddsCapAndSides();
    void insettingFaceAddsInnerCapAndRing();
    void invalidInsetIsAtomic();
    void deleteFaceRemovesSurfaceAndCompactsVertices();
    void deleteVertexRemovesAttachedFacesAndCompactsVertices();
    void invalidVertexDeletionIsAtomic();
    void faceNormalsCanBeFlippedAndRestored();
    void mergingAdjacentVerticesKeepsValidTopology();
    void invalidVertexMergeIsAtomic();
    void createFaceFillsDeletedCubeSurface();
    void invalidFaceCreationDoesNotMutateMesh();
    void invalidExtrusionIsRejected();
    void invalidMeshLoadLeavesCurrentMeshUntouched();
    void meshRoundTripsThroughVariantMap();
};

void EditableGeometryTests::cubeHasEditableTopology()
{
    EditableGeometry geometry;
    QCOMPARE(geometry.shape(), QStringLiteral("Cube"));
    QCOMPARE(geometry.vertexCount(), 8);
    QCOMPARE(geometry.faceCount(), 6);
    QVERIFY(geometry.faceNormal(0).length() > 0.9f);
}

void EditableGeometryTests::edgeIndexExposesUniqueEdgesAndCenters()
{
    EditableGeometry geometry;
    QCOMPARE(geometry.edgeCount(), 12);

    QSet<quint64> seen;
    for (int index = 0; index < geometry.edgeCount(); ++index) {
        const QVariantList endpoints = geometry.edgeVertexIndices(index);
        QCOMPARE(endpoints.size(), 2);
        const int a = endpoints.at(0).toInt();
        const int b = endpoints.at(1).toInt();
        QVERIFY(a >= 0 && a < geometry.vertexCount());
        QVERIFY(b >= 0 && b < geometry.vertexCount());
        QVERIFY(a < b);

        const quint64 key = (static_cast<quint64>(static_cast<quint32>(a)) << 32)
            | static_cast<quint32>(b);
        QVERIFY(!seen.contains(key));
        seen.insert(key);

        const QVector3D expected =
            (geometry.vertexPosition(a) + geometry.vertexPosition(b)) * 0.5f;
        QCOMPARE(geometry.edgeCenter(index), expected);
    }

    QCOMPARE(seen.size(), 12);
    QVERIFY(geometry.edgeVertexIndices(-1).isEmpty());
    QVERIFY(geometry.edgeVertexIndices(geometry.edgeCount()).isEmpty());
    QCOMPARE(geometry.edgeCenter(-1), QVector3D());
}

void EditableGeometryTests::edgeIndexTracksTopologyChanges()
{
    EditableGeometry geometry;
    QCOMPARE(geometry.edgeCount(), 12);

    QVERIFY(geometry.extrudeFace(0, 10.0));
    QCOMPARE(geometry.edgeCount(), 20);

    geometry.setShape(QStringLiteral("Plane"));
    QCOMPARE(geometry.edgeCount(), 4);
}

void EditableGeometryTests::subdividingEdgeAddsOneMidpointAndPreservesFaces()
{
    EditableGeometry geometry;
    const QVariantList endpoints = geometry.edgeVertexIndices(0);
    QCOMPARE(endpoints.size(), 2);
    const QVector3D a = geometry.vertexPosition(endpoints.at(0).toInt());
    const QVector3D b = geometry.vertexPosition(endpoints.at(1).toInt());
    const QVector3D expectedMidpoint = (a + b) * 0.5f;
    const int originalFaces = geometry.faceCount();

    QVERIFY(geometry.subdivideEdge(0));
    QCOMPARE(geometry.vertexCount(), 9);
    QCOMPARE(geometry.faceCount(), originalFaces);
    QCOMPARE(geometry.edgeCount(), 13);
    QCOMPARE(geometry.vertexPosition(8), expectedMidpoint);

    int facesWithMidpoint = 0;
    for (int faceIndex = 0; faceIndex < geometry.faceCount(); ++faceIndex) {
        const QVariantList face = geometry.serializeMesh()
            .value(QStringLiteral("faces")).toList().at(faceIndex).toList();
        if (face.contains(8))
            ++facesWithMidpoint;
        QVERIFY(geometry.faceNormal(faceIndex).length() > 0.9f);
    }
    QCOMPARE(facesWithMidpoint, 2);

    geometry.setShape(QStringLiteral("Plane"));
    QCOMPARE(geometry.edgeCount(), 4);
    QVERIFY(geometry.subdivideEdge(0));
    QCOMPARE(geometry.vertexCount(), 5);
    QCOMPARE(geometry.edgeCount(), 5);
    QCOMPARE(geometry.faceCount(), 1);
}

void EditableGeometryTests::bevelsCubeEdgeAndKeepsClosedTopology()
{
    EditableGeometry geometry;
    QCOMPARE(geometry.vertexCount(), 8);
    QCOMPARE(geometry.faceCount(), 6);
    QCOMPARE(geometry.edgeCount(), 12);

    QVERIFY(geometry.bevelEdge(0, 5.0));
    // The two replaced corner vertices are removed instead of being left orphaned.
    QCOMPARE(geometry.vertexCount(), 10);
    QCOMPARE(geometry.faceCount(), 7);
    QCOMPARE(geometry.edgeCount(), 15);

    for (int face = 0; face < geometry.faceCount(); ++face)
        QVERIFY2(geometry.faceNormal(face).length() > 0.9f,
                 "The bevel must keep every face usable for rendering.");

    const QVariantMap saved = geometry.serializeMesh();
    EditableGeometry restored;
    QVERIFY(restored.loadMesh(saved));
    QCOMPARE(restored.vertexCount(), geometry.vertexCount());
    QCOMPARE(restored.faceCount(), geometry.faceCount());
    QCOMPARE(restored.edgeCount(), geometry.edgeCount());
}

void EditableGeometryTests::invalidEdgeBevelIsAtomic()
{
    EditableGeometry geometry;
    const QVariantMap before = geometry.serializeMesh();

    QVERIFY(!geometry.bevelEdge(-1, 5.0));
    QVERIFY(!geometry.bevelEdge(geometry.edgeCount(), 5.0));
    QVERIFY(!geometry.bevelEdge(0, 0.0));
    QVERIFY(!geometry.bevelEdge(0, -2.0));
    QVERIFY(!geometry.bevelEdge(0, 50.0));
    QCOMPARE(geometry.serializeMesh(), before);

    // An open plane edge has no closed bevel strip, so it is rejected unchanged.
    geometry.setShape(QStringLiteral("Plane"));
    const QVariantMap openBefore = geometry.serializeMesh();
    QVERIFY(!geometry.bevelEdge(0, 5.0));
    QCOMPARE(geometry.serializeMesh(), openBefore);
}

void EditableGeometryTests::invalidEdgeSubdivisionIsAtomic()

{
    EditableGeometry geometry;
    const QVariantMap before = geometry.serializeMesh();
    const int originalEdges = geometry.edgeCount();

    QVERIFY(!geometry.subdivideEdge(-1));
    QVERIFY(!geometry.subdivideEdge(originalEdges));
    QCOMPARE(geometry.serializeMesh(), before);
    QCOMPARE(geometry.edgeCount(), originalEdges);
}
void EditableGeometryTests::closedEdgeLoopCreatesFaceForDeletedSurface()
{
    EditableGeometry geometry;
    QVERIFY(geometry.deleteFace(0));
    const int originalFaceCount = geometry.faceCount();

    QVariantList boundaryEdges;
    for (int edgeIndex = 0; edgeIndex < geometry.edgeCount(); ++edgeIndex) {
        const QVariantList endpoints = geometry.edgeVertexIndices(edgeIndex);
        const int a = endpoints.at(0).toInt();
        const int b = endpoints.at(1).toInt();
        if (a >= 4 && a <= 7 && b >= 4 && b <= 7)
            boundaryEdges.append(edgeIndex);
    }

    QCOMPARE(boundaryEdges.size(), 4);

    // Deliberately put the 4–7 edge first, which produces the opposite winding
    // from the natural +Z cap if ordering depends on which edge was clicked first.
    QVariantList arbitraryOrder;
    for (const QVariant &edgeIndex : boundaryEdges) {
        const QVariantList endpoints = geometry.edgeVertexIndices(edgeIndex.toInt());
        if (endpoints.at(0).toInt() == 4 && endpoints.at(1).toInt() == 7)
            arbitraryOrder.prepend(edgeIndex);
        else
            arbitraryOrder.append(edgeIndex);
    }
    QVERIFY(geometry.createFaceFromEdges(arbitraryOrder));
    QCOMPARE(geometry.faceCount(), originalFaceCount + 1);
    QCOMPARE(geometry.vertexCount(), 8);
    QVERIFY(geometry.faceNormal(geometry.faceCount() - 1).z() > 0.99f);
}

void EditableGeometryTests::invalidEdgeLoopsAreRejectedAtomically()
{
    EditableGeometry openBoundary;
    QVERIFY(openBoundary.deleteFace(0));
    QVariantList topEdges;
    for (int edgeIndex = 0; edgeIndex < openBoundary.edgeCount(); ++edgeIndex) {
        const QVariantList endpoints = openBoundary.edgeVertexIndices(edgeIndex);
        const int a = endpoints.at(0).toInt();
        const int b = endpoints.at(1).toInt();
        if (a >= 4 && a <= 7 && b >= 4 && b <= 7)
            topEdges.append(edgeIndex);
    }
    QCOMPARE(topEdges.size(), 4);

    QVariantMap before = openBoundary.serializeMesh();
    QVariantList openChain = topEdges;
    openChain.removeLast();
    QVERIFY(!openBoundary.createFaceFromEdges(openChain));
    QCOMPARE(openBoundary.serializeMesh(), before);

    QVariantList duplicateEdge{topEdges.first(), topEdges.first(), topEdges.at(1)};
    QVERIFY(!openBoundary.createFaceFromEdges(duplicateEdge));
    QCOMPARE(openBoundary.serializeMesh(), before);

    EditableGeometry disconnected;
    QVariantList twoLoops;
    for (int edgeIndex = 0; edgeIndex < disconnected.edgeCount(); ++edgeIndex) {
        const QVariantList endpoints = disconnected.edgeVertexIndices(edgeIndex);
        const int a = endpoints.at(0).toInt();
        const int b = endpoints.at(1).toInt();
        const bool top = a >= 4 && a <= 7 && b >= 4 && b <= 7;
        const bool bottom = a >= 0 && a <= 3 && b >= 0 && b <= 3;
        if (top || bottom)
            twoLoops.append(edgeIndex);
    }
    QCOMPARE(twoLoops.size(), 8);
    before = disconnected.serializeMesh();
    QVERIFY(!disconnected.createFaceFromEdges(twoLoops));
    QCOMPARE(disconnected.serializeMesh(), before);
}

void EditableGeometryTests::primitiveShapesHaveExpectedTopology()
{
    EditableGeometry geometry;

    geometry.setShape(QStringLiteral("Plane"));
    QCOMPARE(geometry.vertexCount(), 4);
    QCOMPARE(geometry.faceCount(), 1);
    QVERIFY(geometry.faceNormal(0).y() > 0.9f);

    geometry.setShape(QStringLiteral("Cylinder"));
    QCOMPARE(geometry.vertexCount(), 64);
    QCOMPARE(geometry.faceCount(), 34);

    geometry.setShape(QStringLiteral("Cone"));
    QCOMPARE(geometry.vertexCount(), 33);
    QCOMPARE(geometry.faceCount(), 33);

    geometry.setShape(QStringLiteral("Sphere"));
    QCOMPARE(geometry.vertexCount(), 482);
    QCOMPARE(geometry.faceCount(), 512);
}

void EditableGeometryTests::torusHasClosedEditableTopology()
{
    EditableGeometry geometry;
    geometry.setShape(QStringLiteral("Torus"));

    // Each shared grid point belongs to neighboring faces and edges.
    QCOMPARE(geometry.shape(), QStringLiteral("Torus"));
    QCOMPARE(geometry.vertexCount(), 32 * 12);
    QCOMPARE(geometry.faceCount(), 32 * 12);
    QCOMPARE(geometry.edgeCount(), 32 * 12 * 2);

    // The outer and inner walls need opposite normals to light correctly.
    QVERIFY(geometry.faceNormal(0).x() > 0.0f);
    QVERIFY(geometry.faceNormal(6).x() < 0.0f);
    for (int face = 0; face < geometry.faceCount(); ++face)
        QVERIFY(geometry.faceNormal(face).length() > 0.9f);

    const QVariantMap saved = geometry.serializeMesh();
    EditableGeometry restored;
    QVERIFY(restored.loadMesh(saved));
    QCOMPARE(restored.shape(), QStringLiteral("Torus"));
    QCOMPARE(restored.vertexCount(), geometry.vertexCount());
    QCOMPARE(restored.faceCount(), geometry.faceCount());
    QCOMPARE(restored.edgeCount(), geometry.edgeCount());
}

void EditableGeometryTests::smoothShadingStateIsSavedAndRestored()
{
    EditableGeometry geometry;
    geometry.setShape(QStringLiteral("Sphere"));
    QVERIFY(!geometry.smoothShading());

    QSignalSpy changed(&geometry, &EditableGeometry::smoothShadingChanged);
    QVERIFY(changed.isValid());
    geometry.setSmoothShading(true);
    QVERIFY(geometry.smoothShading());
    QCOMPARE(changed.size(), 1);

    const QVariantMap saved = geometry.serializeMesh();
    QVERIFY(saved.value(QStringLiteral("smoothShading")).toBool());

    EditableGeometry restored;
    QVERIFY(restored.loadMesh(saved));
    QVERIFY(restored.smoothShading());
    QCOMPARE(restored.serializeMesh(), saved);

    restored.setSmoothShading(false);
    QVERIFY(!restored.smoothShading());
    QVERIFY(!restored.serializeMesh().value(QStringLiteral("smoothShading")).toBool());
}

void EditableGeometryTests::vertexEditsChangeSerializedMesh()


{
    EditableGeometry geometry;
    const QVector3D replacement(-17.0f, 23.0f, 9.5f);
    QVERIFY(geometry.setVertexPosition(0, replacement));
    QCOMPARE(geometry.vertexPosition(0), replacement);
    QCOMPARE(geometry.serializeMesh().value(QStringLiteral("vertices")).toList().size(), 8);
}

void EditableGeometryTests::invalidVertexEditsAreRejectedAtomically()
{
    EditableGeometry geometry;
    const QVariantMap before = geometry.serializeMesh();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    QVERIFY(!geometry.setVertexPosition(0, QVector3D(nan, 0.0f, 0.0f)));
    QVERIFY(!geometry.setVertexPosition(0, QVector3D(0.0f, infinity, 0.0f)));
    QVERIFY(!geometry.setVertexPosition(0, QVector3D(1.0e6f + 1.0f, 0.0f, 0.0f)));
    QVERIFY(!geometry.setVertexPosition(-1, QVector3D(0.0f, 0.0f, 0.0f)));
    QCOMPARE(geometry.serializeMesh(), before);
}

void EditableGeometryTests::extrudingFaceAddsCapAndSides()
{
    EditableGeometry geometry;
    const QVector3D centerBefore = geometry.faceCenter(0);
    const QVector3D normal = geometry.faceNormal(0);
    QVERIFY(normal.length() > 0.9f);

    QVERIFY(geometry.extrudeFace(0, 10.0));
    QCOMPARE(geometry.vertexCount(), 12);
    QCOMPARE(geometry.faceCount(), 10);

    const QVector3D centerAfter = geometry.faceCenter(0);
    QVERIFY(QVector3D::dotProduct(centerAfter - centerBefore, normal) > 9.9f);
}

void EditableGeometryTests::insettingFaceAddsInnerCapAndRing()
{
    EditableGeometry geometry;
    const QVector3D centerBefore = geometry.faceCenter(0);
    const QVector3D normal = geometry.faceNormal(0);

    QVERIFY(geometry.insetFace(0, 10.0));
    QCOMPARE(geometry.vertexCount(), 12);
    QCOMPARE(geometry.faceCount(), 10);
    QVERIFY((geometry.faceCenter(0) - centerBefore).length() < 1.0e-3f);
    QVERIFY(QVector3D::dotProduct(geometry.faceNormal(0), normal) > 0.99f);
    QCOMPARE(geometry.vertexPosition(8), QVector3D(-40, -40, 50));
}

void EditableGeometryTests::invalidInsetIsAtomic()
{
    EditableGeometry geometry;
    const QVariantMap before = geometry.serializeMesh();

    QVERIFY(!geometry.insetFace(-1, 10.0));
    QVERIFY(!geometry.insetFace(0, 0.0));
    QVERIFY(!geometry.insetFace(0, -2.0));
    QVERIFY(!geometry.insetFace(0, 60.0));
    QCOMPARE(geometry.serializeMesh(), before);
}

void EditableGeometryTests::deleteFaceRemovesSurfaceAndCompactsVertices()
{
    EditableGeometry cube;
    QVERIFY(cube.deleteFace(0));
    QCOMPARE(cube.faceCount(), 5);
    QCOMPARE(cube.vertexCount(), 8);

    EditableGeometry plane;
    plane.setShape(QStringLiteral("Plane"));
    const QVariantMap before = plane.serializeMesh();
    QVERIFY(!plane.deleteFace(0)); // Keep at least one face in a mesh.
    QCOMPARE(plane.serializeMesh(), before);

    QVERIFY(!cube.deleteFace(-1));
    QVERIFY(!cube.deleteFace(cube.faceCount()));
}

void EditableGeometryTests::deleteVertexRemovesAttachedFacesAndCompactsVertices()
{
    EditableGeometry geometry;
    const QVector3D removedPosition = geometry.vertexPosition(0);

    QVERIFY(geometry.deleteVertex(0));
    QCOMPARE(geometry.vertexCount(), 7);
    QCOMPARE(geometry.faceCount(), 3);
    QCOMPARE(geometry.shape(), QStringLiteral("Custom"));

    bool stillContainsRemovedPosition = false;
    for (int index = 0; index < geometry.vertexCount(); ++index) {
        if (geometry.vertexPosition(index) == removedPosition)
            stillContainsRemovedPosition = true;
    }
    QVERIFY(!stillContainsRemovedPosition);

    const QVariantList faces = geometry.serializeMesh().value(QStringLiteral("faces")).toList();
    for (const QVariant &rawFace : faces) {
        const QVariantList face = rawFace.toList();
        QVERIFY(face.size() >= 3);
        for (const QVariant &rawIndex : face) {
            const int index = rawIndex.toInt();
            QVERIFY(index >= 0 && index < geometry.vertexCount());
        }
    }
    for (int faceIndex = 0; faceIndex < geometry.faceCount(); ++faceIndex)
        QVERIFY(geometry.faceNormal(faceIndex).length() > 0.9f);
}

void EditableGeometryTests::invalidVertexDeletionIsAtomic()
{
    EditableGeometry cube;
    const QVariantMap beforeCube = cube.serializeMesh();
    QVERIFY(!cube.deleteVertex(-1));
    QVERIFY(!cube.deleteVertex(cube.vertexCount()));
    QCOMPARE(cube.serializeMesh(), beforeCube);

    EditableGeometry plane;
    plane.setShape(QStringLiteral("Plane"));
    const QVariantMap beforePlane = plane.serializeMesh();
    QVERIFY(!plane.deleteVertex(0)); // Removing it would also remove the only face.
    QCOMPARE(plane.serializeMesh(), beforePlane);
}

void EditableGeometryTests::faceNormalsCanBeFlippedAndRestored()
{
    EditableGeometry geometry;
    const QVariantMap originalMesh = geometry.serializeMesh();
    QVector<QVector3D> originalNormals;
    originalNormals.reserve(geometry.faceCount());
    for (int faceIndex = 0; faceIndex < geometry.faceCount(); ++faceIndex)
        originalNormals.append(geometry.faceNormal(faceIndex));

    QVERIFY(geometry.flipNormals());
    QCOMPARE(geometry.faceCount(), originalNormals.size());
    for (int faceIndex = 0; faceIndex < geometry.faceCount(); ++faceIndex) {
        const QVector3D flippedNormal = geometry.faceNormal(faceIndex);
        QVERIFY(flippedNormal.length() > 0.9f);
        QVERIFY(QVector3D::dotProduct(originalNormals.at(faceIndex), flippedNormal) < -0.99f);
    }

    // Flipping a second time restores the exact original face winding and scene data.
    QVERIFY(geometry.flipNormals());
    QCOMPARE(geometry.serializeMesh(), originalMesh);
}

void EditableGeometryTests::mergingAdjacentVerticesKeepsValidTopology()


{
    EditableGeometry geometry;
    const QVector3D targetPosition = geometry.vertexPosition(1);

    QVERIFY(geometry.mergeVertices(0, 1));
    QCOMPARE(geometry.vertexCount(), 7);
    QCOMPARE(geometry.faceCount(), 6);

    for (int faceIndex = 0; faceIndex < geometry.faceCount(); ++faceIndex) {
        const QVariantList corners = geometry.serializeMesh()
            .value(QStringLiteral("faces")).toList().at(faceIndex).toList();
        QVERIFY(corners.size() >= 3);
        QVERIFY(geometry.faceNormal(faceIndex).length() > 0.9f);
    }

    bool retainedTarget = false;
    for (int index = 0; index < geometry.vertexCount(); ++index) {
        if (geometry.vertexPosition(index) == targetPosition)
            retainedTarget = true;
    }
    QVERIFY(retainedTarget);
}

void EditableGeometryTests::invalidVertexMergeIsAtomic()
{
    EditableGeometry geometry;
    const QVariantMap before = geometry.serializeMesh();

    QVERIFY(!geometry.mergeVertices(-1, 0));
    QVERIFY(!geometry.mergeVertices(0, geometry.vertexCount()));
    QVERIFY(!geometry.mergeVertices(0, 0));
    QCOMPARE(geometry.serializeMesh(), before);
}

void EditableGeometryTests::createFaceFillsDeletedCubeSurface()
{
    EditableGeometry geometry;
    QVERIFY(geometry.deleteFace(0));
    QCOMPARE(geometry.faceCount(), 5);

    const QVariantList corners{4, 5, 6, 7};
    QVERIFY(geometry.createFace(corners));
    QCOMPARE(geometry.faceCount(), 6);
    QCOMPARE(geometry.vertexCount(), 8);
    QVERIFY(geometry.faceNormal(5).length() > 0.9f);
}

void EditableGeometryTests::invalidFaceCreationDoesNotMutateMesh()
{
    EditableGeometry geometry;
    const QVariantMap before = geometry.serializeMesh();

    QVERIFY(!geometry.createFace(QVariantList{0, 1}));
    QVERIFY(!geometry.createFace(QVariantList{0, 1, 1}));
    QVERIFY(!geometry.createFace(QVariantList{0, 1, 2, 99}));
    QCOMPARE(geometry.serializeMesh(), before);
}

void EditableGeometryTests::invalidExtrusionIsRejected()
{
    EditableGeometry geometry;
    QVERIFY(!geometry.extrudeFace(-1, 10.0));
    QVERIFY(!geometry.extrudeFace(0, 0.0));
    QVERIFY(!geometry.extrudeFace(0, std::numeric_limits<double>::infinity()));
    QCOMPARE(geometry.vertexCount(), 8);
    QCOMPARE(geometry.faceCount(), 6);
}

void EditableGeometryTests::invalidMeshLoadLeavesCurrentMeshUntouched()
{
    EditableGeometry geometry;
    const QVariantMap before = geometry.serializeMesh();

    QVariantMap invalid = before;
    invalid.insert(QStringLiteral("vertices"), QVariantList{QVariantList{0, 0}});
    QVERIFY(!geometry.loadMesh(invalid));
    QCOMPARE(geometry.serializeMesh(), before);
}

void EditableGeometryTests::meshRoundTripsThroughVariantMap()
{
    EditableGeometry source;
    QVERIFY(source.setVertexPosition(3, QVector3D(-40, 37, 22)));
    QVERIFY(source.extrudeFace(0, 12.0));

    const QVariantMap serialized = source.serializeMesh();
    EditableGeometry destination;
    QVERIFY(destination.loadMesh(serialized));
    QCOMPARE(destination.vertexCount(), source.vertexCount());
    QCOMPARE(destination.faceCount(), source.faceCount());
    QCOMPARE(destination.vertexPosition(3), source.vertexPosition(3));
    QCOMPARE(destination.serializeMesh(), serialized);
}

QTEST_GUILESS_MAIN(EditableGeometryTests)
#include "test_editablegeometry.moc"
