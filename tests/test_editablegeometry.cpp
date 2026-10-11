#include "editablegeometry.h"

#include <QtTest>

class EditableGeometryTests final : public QObject
{
    Q_OBJECT

private slots:
    void cubeHasEditableTopology();
    void primitiveShapesHaveExpectedTopology();
    void vertexEditsChangeSerializedMesh();
    void extrudingFaceAddsCapAndSides();
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
