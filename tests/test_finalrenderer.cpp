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
    void smoothShadingChangesFinalRenderAppearance();
    void keyLightRotationChangesFinalRenderAppearance();
    void fillLightPositionChangesFinalRenderAppearance();
    void fillLightColorChangesFinalRenderAppearance();
    void backgroundColorChangesFinalRenderAppearance();
    void transparentBackgroundWritesAlphaToPng();
    void orthographicProjectionChangesFinalRender();
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

void FinalRendererTests::keyLightRotationChangesFinalRenderAppearance()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto sceneWithKeyLightX = [](double x) {
        QVariantMap scene = smallPlaneScene();
        QVariantMap settings = scene.value(QStringLiteral("settings")).toMap();
        settings.insert(QStringLiteral("keyLightRotationX"), x);
        settings.insert(QStringLiteral("keyLightRotationY"), -35.0);
        settings.insert(QStringLiteral("keyLightRotationZ"), 0.0);
        scene.insert(QStringLiteral("settings"), settings);
        return scene;
    };

    FinalRenderer renderer;
    QSignalSpy finished(&renderer, &FinalRenderer::renderFinished);
    QVERIFY(finished.isValid());

    const QString upperPath = directory.filePath(QStringLiteral("key-light-upper.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(upperPath),
                                 sceneWithKeyLightX(-42.0), 64, 64, 2, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> upperResult = finished.takeFirst();
    QVERIFY2(upperResult.at(0).toBool(), qPrintable(upperResult.at(1).toString()));
    const QImage upperImage(upperPath);
    QVERIFY(!upperImage.isNull());

    const QString lowerPath = directory.filePath(QStringLiteral("key-light-lower.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(lowerPath),
                                 sceneWithKeyLightX(40.0), 64, 64, 2, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> lowerResult = finished.takeFirst();
    QVERIFY2(lowerResult.at(0).toBool(), qPrintable(lowerResult.at(1).toString()));
    const QImage lowerImage(lowerPath);
    QVERIFY(!lowerImage.isNull());

    qint64 totalPixelDifference = 0;
    for (int y = 0; y < upperImage.height(); ++y) {
        const auto *upperRow = reinterpret_cast<const QRgb *>(upperImage.constScanLine(y));
        const auto *lowerRow = reinterpret_cast<const QRgb *>(lowerImage.constScanLine(y));
        for (int x = 0; x < upperImage.width(); ++x) {
            totalPixelDifference += std::abs(qRed(upperRow[x]) - qRed(lowerRow[x]));
            totalPixelDifference += std::abs(qGreen(upperRow[x]) - qGreen(lowerRow[x]));
            totalPixelDifference += std::abs(qBlue(upperRow[x]) - qBlue(lowerRow[x]));
        }
    }

    QVERIFY2(totalPixelDifference > 100,
             "Changing the directional key-light rotation must change the final image.");
    QVERIFY(!renderer.busy());
}

void FinalRendererTests::fillLightColorChangesFinalRenderAppearance()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto sceneWithFillColor = [](const QString &color) {
        QVariantMap scene = smallPlaneScene();
        QVariantMap settings = scene.value(QStringLiteral("settings")).toMap();
        settings.insert(QStringLiteral("fillLightX"), -160.0);
        settings.insert(QStringLiteral("fillLightY"), 190.0);
        settings.insert(QStringLiteral("fillLightZ"), 140.0);
        settings.insert(QStringLiteral("fillLightBrightness"), 150.0);
        settings.insert(QStringLiteral("fillLightColor"), color);
        scene.insert(QStringLiteral("settings"), settings);
        return scene;
    };

    FinalRenderer renderer;
    QSignalSpy finished(&renderer, &FinalRenderer::renderFinished);
    QVERIFY(finished.isValid());

    const QString redPath = directory.filePath(QStringLiteral("fill-light-red.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(redPath),
                                 sceneWithFillColor(QStringLiteral("#ff2020")), 64, 64, 2, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> redResult = finished.takeFirst();
    QVERIFY2(redResult.at(0).toBool(), qPrintable(redResult.at(1).toString()));
    const QImage redImage(redPath);
    QVERIFY(!redImage.isNull());

    const QString bluePath = directory.filePath(QStringLiteral("fill-light-blue.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(bluePath),
                                 sceneWithFillColor(QStringLiteral("#2040ff")), 64, 64, 2, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> blueResult = finished.takeFirst();
    QVERIFY2(blueResult.at(0).toBool(), qPrintable(blueResult.at(1).toString()));
    const QImage blueImage(bluePath);
    QVERIFY(!blueImage.isNull());

    qint64 totalPixelDifference = 0;
    for (int y = 0; y < redImage.height(); ++y) {
        const auto *redRow = reinterpret_cast<const QRgb *>(redImage.constScanLine(y));
        const auto *blueRow = reinterpret_cast<const QRgb *>(blueImage.constScanLine(y));
        for (int x = 0; x < redImage.width(); ++x) {
            totalPixelDifference += std::abs(qRed(redRow[x]) - qRed(blueRow[x]));
            totalPixelDifference += std::abs(qGreen(redRow[x]) - qGreen(blueRow[x]));
            totalPixelDifference += std::abs(qBlue(redRow[x]) - qBlue(blueRow[x]));
        }
    }

    QVERIFY2(totalPixelDifference > 100,
             "A custom fill-light color must affect final render pixels.");
    QVERIFY(!renderer.busy());
}

void FinalRendererTests::fillLightPositionChangesFinalRenderAppearance()


{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto sceneWithFillLightX = [](double x) {
        QVariantMap scene = smallPlaneScene();
        QVariantMap settings = scene.value(QStringLiteral("settings")).toMap();
        settings.insert(QStringLiteral("fillLightX"), x);
        settings.insert(QStringLiteral("fillLightY"), 180.0);
        settings.insert(QStringLiteral("fillLightZ"), 140.0);
        scene.insert(QStringLiteral("settings"), settings);
        return scene;
    };

    FinalRenderer renderer;
    QSignalSpy finished(&renderer, &FinalRenderer::renderFinished);
    QVERIFY(finished.isValid());

    const QString nearPath = directory.filePath(QStringLiteral("fill-light-near.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(nearPath),
                                 sceneWithFillLightX(-160.0), 64, 64, 2, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> nearResult = finished.takeFirst();
    QVERIFY2(nearResult.at(0).toBool(), qPrintable(nearResult.at(1).toString()));
    const QImage nearImage(nearPath);
    QVERIFY(!nearImage.isNull());

    const QString farPath = directory.filePath(QStringLiteral("fill-light-far.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(farPath),
                                 sceneWithFillLightX(800.0), 64, 64, 2, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> farResult = finished.takeFirst();
    QVERIFY2(farResult.at(0).toBool(), qPrintable(farResult.at(1).toString()));
    const QImage farImage(farPath);
    QVERIFY(!farImage.isNull());

    qint64 totalPixelDifference = 0;
    for (int y = 0; y < nearImage.height(); ++y) {
        const auto *nearRow = reinterpret_cast<const QRgb *>(nearImage.constScanLine(y));
        const auto *farRow = reinterpret_cast<const QRgb *>(farImage.constScanLine(y));
        for (int x = 0; x < nearImage.width(); ++x) {
            totalPixelDifference += std::abs(qRed(nearRow[x]) - qRed(farRow[x]));
            totalPixelDifference += std::abs(qGreen(nearRow[x]) - qGreen(farRow[x]));
            totalPixelDifference += std::abs(qBlue(nearRow[x]) - qBlue(farRow[x]));
        }
    }

    QVERIFY2(totalPixelDifference > 100,
             "Changing the saved fill-light X position must affect final render lighting.");
    QVERIFY(!renderer.busy());
}

void FinalRendererTests::backgroundColorChangesFinalRenderAppearance()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto sceneWithBackground = [](const QString &color) {
        QVariantMap scene = smallPlaneScene();
        QVariantMap settings = scene.value(QStringLiteral("settings")).toMap();
        settings.insert(QStringLiteral("backgroundColor"), color);
        scene.insert(QStringLiteral("settings"), settings);
        return scene;
    };

    FinalRenderer renderer;
    QSignalSpy finished(&renderer, &FinalRenderer::renderFinished);
    QVERIFY(finished.isValid());

    const QString darkPath = directory.filePath(QStringLiteral("background-dark.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(darkPath),
                                 sceneWithBackground(QStringLiteral("#171717")), 64, 64, 1, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> darkResult = finished.takeFirst();
    QVERIFY2(darkResult.at(0).toBool(), qPrintable(darkResult.at(1).toString()));
    const QImage darkImage(darkPath);
    QVERIFY(!darkImage.isNull());

    const QString purplePath = directory.filePath(QStringLiteral("background-purple.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(purplePath),
                                 sceneWithBackground(QStringLiteral("#7842a8")), 64, 64, 1, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> purpleResult = finished.takeFirst();
    QVERIFY2(purpleResult.at(0).toBool(), qPrintable(purpleResult.at(1).toString()));
    const QImage purpleImage(purplePath);
    QVERIFY(!purpleImage.isNull());

    qint64 totalPixelDifference = 0;
    for (int y = 0; y < darkImage.height(); ++y) {
        const auto *darkRow = reinterpret_cast<const QRgb *>(darkImage.constScanLine(y));
        const auto *purpleRow = reinterpret_cast<const QRgb *>(purpleImage.constScanLine(y));
        for (int x = 0; x < darkImage.width(); ++x) {
            totalPixelDifference += std::abs(qRed(darkRow[x]) - qRed(purpleRow[x]));
            totalPixelDifference += std::abs(qGreen(darkRow[x]) - qGreen(purpleRow[x]));
            totalPixelDifference += std::abs(qBlue(darkRow[x]) - qBlue(purpleRow[x]));
        }
    }

    QVERIFY2(totalPixelDifference > 100,
             "The chosen scene background colour must appear in final-render pixels.");
    QVERIFY(!renderer.busy());
}

void FinalRendererTests::transparentBackgroundWritesAlphaToPng()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto sceneWithTransparency = [](bool transparent) {
        QVariantMap scene = smallPlaneScene();
        QVariantMap settings = scene.value(QStringLiteral("settings")).toMap();
        settings.insert(QStringLiteral("transparentBackground"), transparent);
        settings.insert(QStringLiteral("backgroundColor"), QStringLiteral("#7842a8"));
        scene.insert(QStringLiteral("settings"), settings);
        return scene;
    };

    FinalRenderer renderer;
    QSignalSpy finished(&renderer, &FinalRenderer::renderFinished);
    QVERIFY(finished.isValid());

    const QString transparentPath = directory.filePath(QStringLiteral("transparent.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(transparentPath),
                                 sceneWithTransparency(true), 64, 64, 4, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> transparentResult = finished.takeFirst();
    QVERIFY2(transparentResult.at(0).toBool(),
             qPrintable(transparentResult.at(1).toString()));
    const QImage transparentImage(transparentPath);
    QVERIFY(!transparentImage.isNull());

    int clearPixels = 0;
    int coveredPixels = 0;
    int antialiasedPixels = 0;
    for (int y = 0; y < transparentImage.height(); ++y) {
        for (int x = 0; x < transparentImage.width(); ++x) {
            const int alpha = qAlpha(transparentImage.pixel(x, y));
            if (alpha == 0)
                ++clearPixels;
            else if (alpha == 255)
                ++coveredPixels;
            else
                ++antialiasedPixels;
        }
    }

    QVERIFY2(clearPixels > 0,
             "Pixels outside the model must be completely transparent.");
    QVERIFY2(coveredPixels > 0,
             "Visible geometry pixels must remain opaque.");
    QVERIFY2(antialiasedPixels > 0,
             "Multi-sample edge coverage should create partially transparent boundary pixels.");

    const QString opaquePath = directory.filePath(QStringLiteral("opaque.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(opaquePath),
                                 sceneWithTransparency(false), 64, 64, 2, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> opaqueResult = finished.takeFirst();
    QVERIFY2(opaqueResult.at(0).toBool(),
             qPrintable(opaqueResult.at(1).toString()));
    const QImage opaqueImage(opaquePath);
    QVERIFY(!opaqueImage.isNull());

    for (int y = 0; y < opaqueImage.height(); ++y) {
        for (int x = 0; x < opaqueImage.width(); ++x)
            QCOMPARE(qAlpha(opaqueImage.pixel(x, y)), 255);
    }
    QVERIFY(!renderer.busy());
}

void FinalRendererTests::orthographicProjectionChangesFinalRender()



{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QVariantMap perspectiveScene = smallPlaneScene();
    QVariantMap settings = perspectiveScene.value(QStringLiteral("settings")).toMap();
    settings.insert(QStringLiteral("orthographicMode"), false);
    perspectiveScene.insert(QStringLiteral("settings"), settings);

    QVariantMap orthographicScene = perspectiveScene;
    settings.insert(QStringLiteral("orthographicMode"), true);
    orthographicScene.insert(QStringLiteral("settings"), settings);

    FinalRenderer renderer;
    QSignalSpy finished(&renderer, &FinalRenderer::renderFinished);
    QVERIFY(finished.isValid());

    const QString perspectivePath = directory.filePath(QStringLiteral("perspective.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(perspectivePath),
                                 perspectiveScene, 64, 64, 1, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> perspectiveResult = finished.takeFirst();
    QVERIFY2(perspectiveResult.at(0).toBool(),
             qPrintable(perspectiveResult.at(1).toString()));
    const QImage perspectiveImage(perspectivePath);
    QVERIFY(!perspectiveImage.isNull());

    const QString orthographicPath = directory.filePath(QStringLiteral("orthographic.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(orthographicPath),
                                 orthographicScene, 64, 64, 1, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> orthographicResult = finished.takeFirst();
    QVERIFY2(orthographicResult.at(0).toBool(),
             qPrintable(orthographicResult.at(1).toString()));
    const QImage orthographicImage(orthographicPath);
    QVERIFY(!orthographicImage.isNull());

    qint64 totalPixelDifference = 0;
    for (int y = 0; y < perspectiveImage.height(); ++y) {
        const auto *perspectiveRow =
            reinterpret_cast<const QRgb *>(perspectiveImage.constScanLine(y));
        const auto *orthographicRow =
            reinterpret_cast<const QRgb *>(orthographicImage.constScanLine(y));
        for (int x = 0; x < perspectiveImage.width(); ++x) {
            totalPixelDifference += std::abs(qRed(perspectiveRow[x]) - qRed(orthographicRow[x]));
            totalPixelDifference += std::abs(qGreen(perspectiveRow[x]) - qGreen(orthographicRow[x]));
            totalPixelDifference += std::abs(qBlue(perspectiveRow[x]) - qBlue(orthographicRow[x]));
        }
    }

    QVERIFY2(totalPixelDifference > 100,
             "Orthographic mode must change the rendered projection, not only its saved setting.");
    QVERIFY(!renderer.busy());
}

void FinalRendererTests::smoothShadingChangesFinalRenderAppearance()

{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto makeCubeScene = [](bool smooth) {
        QVariantMap scene = smallPlaneScene();
        QVariantList objects = scene.value(QStringLiteral("objects")).toList();
        QVariantMap object = objects.first().toMap();

        QVariantList vertices;
        const QVector<QVector3D> points{
            {-50, -50, -50}, {50, -50, -50}, {50, 50, -50}, {-50, 50, -50},
            {-50, -50, 50}, {50, -50, 50}, {50, 50, 50}, {-50, 50, 50}
        };
        for (const QVector3D &point : points) {
            vertices.append(QVariant::fromValue(QVariantList{
                static_cast<double>(point.x()),
                static_cast<double>(point.y()),
                static_cast<double>(point.z())
            }));
        }

        QVariantList faces;
        const QVector<QVariantList> polygons{
            QVariantList{4, 5, 6, 7}, QVariantList{1, 0, 3, 2},
            QVariantList{5, 1, 2, 6}, QVariantList{0, 4, 7, 3},
            QVariantList{3, 7, 6, 2}, QVariantList{0, 1, 5, 4}
        };
        for (const QVariantList &face : polygons)
            faces.append(QVariant::fromValue(face));

        const QVariantMap mesh{
            {QStringLiteral("shape"), QStringLiteral("Cube")},
            {QStringLiteral("vertices"), vertices},
            {QStringLiteral("faces"), faces},
            {QStringLiteral("smoothShading"), smooth}
        };
        object.insert(QStringLiteral("shape"), QStringLiteral("Cube"));
        object.insert(QStringLiteral("label"), QStringLiteral("Shading Cube"));
        object.insert(QStringLiteral("mesh"), mesh);
        scene.insert(QStringLiteral("objects"), QVariantList{object});
        return scene;
    };

    FinalRenderer renderer;
    QSignalSpy finished(&renderer, &FinalRenderer::renderFinished);
    QVERIFY(finished.isValid());

    const QString flatPath = directory.filePath(QStringLiteral("flat-cube.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(flatPath),
                                 makeCubeScene(false), 48, 48, 2, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> flatResult = finished.takeFirst();
    QVERIFY2(flatResult.at(0).toBool(), qPrintable(flatResult.at(1).toString()));
    const QImage flatImage(flatPath);
    QVERIFY(!flatImage.isNull());

    const QString smoothPath = directory.filePath(QStringLiteral("smooth-cube.png"));
    QVERIFY(renderer.startRender(QUrl::fromLocalFile(smoothPath),
                                 makeCubeScene(true), 48, 48, 2, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    const QList<QVariant> smoothResult = finished.takeFirst();
    QVERIFY2(smoothResult.at(0).toBool(), qPrintable(smoothResult.at(1).toString()));
    const QImage smoothImage(smoothPath);
    QVERIFY(!smoothImage.isNull());

    qint64 totalPixelDifference = 0;
    for (int y = 0; y < flatImage.height(); ++y) {
        const auto *flatRow = reinterpret_cast<const QRgb *>(flatImage.constScanLine(y));
        const auto *smoothRow = reinterpret_cast<const QRgb *>(smoothImage.constScanLine(y));
        for (int x = 0; x < flatImage.width(); ++x) {
            totalPixelDifference += std::abs(qRed(flatRow[x]) - qRed(smoothRow[x]));
            totalPixelDifference += std::abs(qGreen(flatRow[x]) - qGreen(smoothRow[x]));
            totalPixelDifference += std::abs(qBlue(flatRow[x]) - qBlue(smoothRow[x]));
        }
    }

    QVERIFY2(totalPixelDifference > 1000,
             "The smooth-shading setting must change the final render, not only the viewport.");
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
