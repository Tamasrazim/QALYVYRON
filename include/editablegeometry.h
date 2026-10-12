#pragma once

#include <QQuick3DGeometry>
#include <QPair>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>

class EditableGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString shape READ shape WRITE setShape NOTIFY shapeChanged)
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY meshChanged)
    Q_PROPERTY(int edgeCount READ edgeCount NOTIFY meshChanged)
    Q_PROPERTY(int faceCount READ faceCount NOTIFY meshChanged)

public:
    explicit EditableGeometry(QQuick3DObject *parent = nullptr);

    QString shape() const;
    void setShape(const QString &shape);
    int vertexCount() const;
    int edgeCount() const;
    int faceCount() const;

    Q_INVOKABLE QVector3D vertexPosition(int index) const;
    Q_INVOKABLE QVariantList edgeVertexIndices(int index) const;
    Q_INVOKABLE QVector3D edgeCenter(int index) const;
    Q_INVOKABLE bool setVertexPosition(int index, const QVector3D &position);
    Q_INVOKABLE QVector3D faceCenter(int index) const;
    Q_INVOKABLE QVector3D faceNormal(int index) const;
    Q_INVOKABLE bool extrudeFace(int faceIndex, double distance);
    Q_INVOKABLE bool insetFace(int faceIndex, double distance);
    Q_INVOKABLE bool deleteFace(int faceIndex);
    Q_INVOKABLE bool mergeVertices(int sourceIndex, int targetIndex);
    Q_INVOKABLE bool createFace(const QVariantList &vertexIndices);
    Q_INVOKABLE QVariantMap serializeMesh() const;
    Q_INVOKABLE bool loadMesh(const QVariantMap &data);

signals:
    void shapeChanged();
    void meshChanged();

private:
    static constexpr int kMaximumVertices = 100000;
    static constexpr int kMaximumFaces = 100000;
    static constexpr int kMaximumFaceCorners = 4096;

    QString m_shape = QStringLiteral("Cube");
    QVector<QVector3D> m_vertices;
    QVector<QVector<int>> m_faces;
    QVector<QPair<int, int>> m_edges;

    void createPrimitive(const QString &shape);
    void orientClosedMeshOutward();
    void rebuildGeometry();
    void rebuildEdgeIndex();
    QVector3D centerForFace(const QVector<int> &face) const;
    QVector3D normalForFace(const QVector<int> &face) const;
    bool validFace(const QVector<int> &face, int vertexCount) const;
};
