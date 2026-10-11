#include "finalrenderer.h"

#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <QSet>

namespace
{
QVariantMap smallPlaneScene()
{
    QVariantList vertices;
    vertices.append(QVariant::fromValue(QVariantList{-50.0, 0.0, -50.0}));
    vertices.append(QVariant::fromValue(QVariantList{ 50.0, 0.0, -50.0}));
    vertices.append(QVariant::fromValue(QVariantList{ 50.0, 0.0,  50.0}));
    vertices.append(QVariant::fromValue(QVariantList{-50.0, 0.0,  50.0}));

    QVariantList faces;
    faces.append(QVariant::fromValue(QVariantList{0, 3, 2, 1}));

    const QVariantMap mesh{
        {QStringLiteral("shape"), QStringLiteral("Plane")},
        {QStringLiteral("vertices"), vertices},
        {QStringLiteral("faces"), faces}
    };
    const QVariantMap object{
        {QStringLiteral("id"), QStringLiteral("object-1")},
        {QStringLiteral("label"), QStringLiteral("Render Plane")},
        {QStringLiteral("shape"), QStringLiteral("Plane")},
        {QStringLiteral("kind"), QStringLiteral("primitive")},
        {QStringLiteral("mesh"), mesh},
        {QStringLiteral("x"), 0.0},
        {QStringLiteral("y"), 0.0},
        {QStringLiteral("z"), 0.0},
        {QStringLiteral("rx"), 0.0},
        {QStringLiteral("ry"), 0.0},
        {QStringLiteral("rz"), 0.0},
        {QStringLiteral("scale"), 1.0},
        {QStringLiteral("color"), QStringLiteral("#f2f2f2")},
        {QStringLiteral("roughness"), 0.4}
    };
    return {
        {QStringLiteral("format"), QStringLiteral("qalyvyron.scene")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("settings"), QVariantMap{
             {QStringLiteral("cameraFov"), 45.0},
             {QStringLiteral("orbitYaw"), 34.0},
             {QStringLiteral("orbitPitch"), 23.0},
             {QStringLiteral("orbitDistance"), 470.0},
             {QStringLiteral("keyLightBrightness"), 1.7},
             {QStringLiteral("fillLightBrightness"), 65.0}
         }},
        {QStringLiteral("objects"), QVariantList{object}}
    };
}
}

class FinalRendererTests final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsInvalidRenderDimensions();
    void writesPngFromSceneGeometry();
};

void FinalRendererTests::rejectsInvalidRenderDimensions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    FinalRenderer renderer;
    const QUrl output = QUrl::fromLocalFile(
        directory.filePath(QStringLiteral("invalid.png")));

    QVERIFY(!renderer.startRender(output, smallPlaneScene(), 0, 32, 1, 0));
    QVERIFY(!renderer.busy());
    QVERIFY(!QFileInfo::exists(output.toLocalFile()));

    QVERIFY(!renderer.startRender(output, smallPlaneScene(), 8192, 8192, 1, 0));
    QVERIFY(!renderer.startRender(output, smallPlaneScene(), 32, 32, 0, 0));
}

void FinalRendererTests::writesPngFromSceneGeometry()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    FinalRenderer renderer;
    QSignalSpy finished(&renderer, &FinalRenderer::renderFinished);
    QVERIFY(finished.isValid());

    const QString path = directory.filePath(QStringLiteral("final-render.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(path),
                                 smallPlaneScene(), 48, 48, 2, 1));

    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> result = finished.takeFirst();
    QVERIFY2(result.at(0).toBool(), qPrintable(result.at(1).toString()));
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(QFileInfo(path).size() > 100);

    const QImage image(path);
    QVERIFY(!image.isNull());
    QCOMPARE(image.width(), 48);
    QCOMPARE(image.height(), 48);

    QSet<QRgb> uniqueColors;
    for (int y = 0; y < image.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            uniqueColors.insert(row[x]);
    }
    QVERIFY2(uniqueColors.size() > 1,
             "The renderer must produce geometry shading, not a flat viewport placeholder.");
    QVERIFY(!renderer.busy());
    QCOMPARE(renderer.progress(), 100.0);
}

QTEST_GUILESS_MAIN(FinalRendererTests)
#include "test_finalrenderer.moc"
