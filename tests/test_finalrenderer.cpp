// What this does: Checks that final renders produce the expected output and that the renderer handles invalid or oversized jobs safely.

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
    void rejectsUnsafeRenderWorkloads();
    void cancelledRenderDoesNotWritePartialOutput();
    void hiddenObjectsAreNotIncludedInFinalRender();
    void offlineRenderRespectsViewportFocusTarget();
    void metalnessChangesFinalRenderAppearance();
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

void FinalRendererTests::rejectsUnsafeRenderWorkloads()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    FinalRenderer renderer;
    const QUrl output = QUrl::fromLocalFile(
        directory.filePath(QStringLiteral("too-expensive.png")));

    QVERIFY(!renderer.startRender(output, smallPlaneScene(), 1920, 1080, 64, 4));
    QVERIFY(!renderer.startRender(output, smallPlaneScene(), 64, 64, 1, 5));
    QVERIFY(!renderer.busy());
    QVERIFY(!QFileInfo::exists(output.toLocalFile()));
}

void FinalRendererTests::cancelledRenderDoesNotWritePartialOutput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    FinalRenderer renderer;
    QSignalSpy finished(&renderer, &FinalRenderer::renderFinished);
    QVERIFY(finished.isValid());

    const QString path = directory.filePath(QStringLiteral("cancelled-render.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(path),
                                 smallPlaneScene(), 640, 360, 16, 3));
    renderer.cancelRender();

    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> result = finished.takeFirst();
    QVERIFY(!result.at(0).toBool());
    QVERIFY(result.at(1).toString().contains(QStringLiteral("cancel"), Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(path));
    QVERIFY(!renderer.busy());
}

void FinalRendererTests::hiddenObjectsAreNotIncludedInFinalRender()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // Hide the only mesh. A final render must not use geometry hidden in the editor.
    QVariantMap scene = smallPlaneScene();
    QVariantList objects = scene.value(QStringLiteral("objects")).toList();
    QVERIFY(!objects.isEmpty());
    QVariantMap hiddenObject = objects.first().toMap();
    hiddenObject.insert(QStringLiteral("visible"), false);
    scene.insert(QStringLiteral("objects"), QVariantList{hiddenObject});

    FinalRenderer renderer;
    QSignalSpy finished(&renderer, &FinalRenderer::renderFinished);
    QVERIFY(finished.isValid());

    const QString path = directory.filePath(QStringLiteral("hidden-object.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(path), scene, 48, 48, 1, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);

    const QList<QVariant> result = finished.takeFirst();
    QVERIFY(!result.at(0).toBool());
    QVERIFY(result.at(1).toString().contains(QStringLiteral("No editable mesh geometry"),
                                             Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(path));
    QVERIFY(!renderer.busy());
}

void FinalRendererTests::offlineRenderRespectsViewportFocusTarget()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // Move the only mesh away from origin and frame that same world position.
    QVariantMap scene = smallPlaneScene();
    QVariantList objects = scene.value(QStringLiteral("objects")).toList();
    QVERIFY(!objects.isEmpty());
    QVariantMap shiftedObject = objects.first().toMap();
    shiftedObject.insert(QStringLiteral("x"), 300.0);
    scene.insert(QStringLiteral("objects"), QVariantList{shiftedObject});

    QVariantMap settings = scene.value(QStringLiteral("settings")).toMap();
    settings.insert(QStringLiteral("orbitTargetX"), 300.0);
    settings.insert(QStringLiteral("orbitTargetY"), 0.0);
    settings.insert(QStringLiteral("orbitTargetZ"), 0.0);
    scene.insert(QStringLiteral("settings"), settings);

    FinalRenderer renderer;
    QSignalSpy finished(&renderer, &FinalRenderer::renderFinished);
    QVERIFY(finished.isValid());

    const QString path = directory.filePath(QStringLiteral("focused-off-origin-render.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(path), scene, 48, 48, 2, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);

    const QList<QVariant> result = finished.takeFirst();
    QVERIFY2(result.at(0).toBool(), qPrintable(result.at(1).toString()));
    const QImage image(path);
    QVERIFY(!image.isNull());

    // The off-origin mesh must be in frame; a camera mistakenly aimed at (0, 0, 0)
    // would produce only the uniform background for this deliberately shifted scene.
    QSet<QRgb> uniqueColors;
    for (int y = 0; y < image.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            uniqueColors.insert(row[x]);
    }
    QVERIFY2(uniqueColors.size() > 1,
             "The final camera must render the mesh at the saved viewport focus target.");
    QVERIFY(!renderer.busy());
}

void FinalRendererTests::metalnessChangesFinalRenderAppearance()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto makeMaterialScene = [](double metalness) {
        QVariantMap scene = smallPlaneScene();
        QVariantList objects = scene.value(QStringLiteral("objects")).toList();
        QVariantMap object = objects.first().toMap();
        object.insert(QStringLiteral("color"), QStringLiteral("#df5b57"));
        object.insert(QStringLiteral("metalness"), metalness);
        objects[0] = object;
        scene.insert(QStringLiteral("objects"), objects);
        return scene;
    };

    FinalRenderer renderer;
    QSignalSpy finished(&renderer, &FinalRenderer::renderFinished);
    QVERIFY(finished.isValid());

    const QString nonMetalPath = directory.filePath(QStringLiteral("non-metal.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(nonMetalPath),
                                 makeMaterialScene(0.0), 48, 48, 2, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> nonMetalResult = finished.takeFirst();
    QVERIFY2(nonMetalResult.at(0).toBool(), qPrintable(nonMetalResult.at(1).toString()));
    const QImage nonMetalImage(nonMetalPath);
    QVERIFY(!nonMetalImage.isNull());

    const QString metalPath = directory.filePath(QStringLiteral("metal.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(metalPath),
                                 makeMaterialScene(1.0), 48, 48, 2, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> metalResult = finished.takeFirst();
    QVERIFY2(metalResult.at(0).toBool(), qPrintable(metalResult.at(1).toString()));
    const QImage metalImage(metalPath);
    QVERIFY(!metalImage.isNull());

    qint64 totalPixelDifference = 0;
    for (int y = 0; y < nonMetalImage.height(); ++y) {
        const auto *nonMetalRow =
            reinterpret_cast<const QRgb *>(nonMetalImage.constScanLine(y));
        const auto *metalRow =
            reinterpret_cast<const QRgb *>(metalImage.constScanLine(y));
        for (int x = 0; x < nonMetalImage.width(); ++x) {
            totalPixelDifference += std::abs(qRed(nonMetalRow[x]) - qRed(metalRow[x]));
            totalPixelDifference += std::abs(qGreen(nonMetalRow[x]) - qGreen(metalRow[x]));
            totalPixelDifference += std::abs(qBlue(nonMetalRow[x]) - qBlue(metalRow[x]));
        }
    }

    QVERIFY2(totalPixelDifference > 1000,
             "Metalness must visibly affect final rendering, not just the inspector value.");
    QVERIFY(!renderer.busy());
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
