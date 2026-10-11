#include "editablegeometry.h"

#include <QtTest>

#include <limits>

class EditableGeometryTests final : public QObject
{
    Q_OBJECT

private slots:
    void cubeHasEditableTopology();
    void primitiveShapesHaveExpectedTopology();
    void vertexEditsChangeSerializedMesh();
    void extrudingFaceAddsCapAndSides();
    void insettingFaceAddsInnerCapAndRing();
    void invalidInsetIsAtomic();
    void deleteFaceRemovesSurfaceAndCompactsVertices();
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

void EditableGeometryTests::vertexEditsChangeSerializedMesh()
{
    EditableGeometry geometry;
    const QVector3D replacement(-17.0f, 23.0f, 9.5f);
    QVERIFY(geometry.setVertexPosition(0, replacement));
    QCOMPARE(geometry.vertexPosition(0), replacement);
    QCOMPARE(geometry.serializeMesh().value(QStringLiteral("vertices")).toList().size(), 8);
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
