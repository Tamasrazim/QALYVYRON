// What this does: Checks that scene files and OBJ files can be saved, reopened, and rejected cleanly when their contents are not valid.

#include "projectio.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class ProjectIOTests final : public QObject
{
    Q_OBJECT

private slots:
    void savesAndLoadsScene();
    void rejectsUnknownFormat();
    void rejectsMalformedJson();
    void importsObjAndExportsWorldTransformedMeshes();
    void rejectsMalformedObjFaces();
    void importsAsciiStlAndExportsWorldTransformedBinaryStl();
    void rejectsMalformedStl();
    void remembersWelcomePreference();
};

void ProjectIOTests::savesAndLoadsScene()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    ProjectIO io;
    const QUrl url = QUrl::fromLocalFile(directory.filePath(QStringLiteral("sample.qvscene")));

    const QVariantMap object{
        {QStringLiteral("id"), QStringLiteral("object-1")},
        {QStringLiteral("label"), QStringLiteral("Test Cube")},
        {QStringLiteral("shape"), QStringLiteral("Cube")},
        {QStringLiteral("x"), 1.5},
        {QStringLiteral("y"), 0.0},
        {QStringLiteral("z"), -2.0},
        {QStringLiteral("scale"), 1.0},
        {QStringLiteral("color"), QStringLiteral("#c8c8c8")},
        {QStringLiteral("roughness"), 0.38}
    };

    const QVariantMap document{
        {QStringLiteral("format"), QStringLiteral("qalyvyron.scene")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("title"), QStringLiteral("Test scene")},
        {QStringLiteral("objects"), QVariantList{object}}
    };

    const QVariantMap saved = io.saveProject(url, document);
    QVERIFY2(saved.value(QStringLiteral("ok")).toBool(),
             qPrintable(saved.value(QStringLiteral("error")).toString()));
    QVERIFY(QFile::exists(url.toLocalFile()));

    const QVariantMap loaded = io.loadProject(url);
    QVERIFY2(loaded.value(QStringLiteral("ok")).toBool(),
             qPrintable(loaded.value(QStringLiteral("error")).toString()));

    const QVariantMap loadedDocument = loaded.value(QStringLiteral("data")).toMap();
    QCOMPARE(loadedDocument.value(QStringLiteral("format")).toString(),
             QStringLiteral("qalyvyron.scene"));
    QCOMPARE(loadedDocument.value(QStringLiteral("version")).toInt(), 1);
    QCOMPARE(loadedDocument.value(QStringLiteral("objects")).toList().size(), 1);
}

void ProjectIOTests::rejectsUnknownFormat()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    ProjectIO io;
    const QUrl url = QUrl::fromLocalFile(directory.filePath(QStringLiteral("wrong.qvscene")));
    const QVariantMap document{
        {QStringLiteral("format"), QStringLiteral("other.format")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("objects"), QVariantList{}}
    };

    const QVariantMap result = io.saveProject(url, document);
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    QVERIFY(!QFile::exists(url.toLocalFile()));
}

void ProjectIOTests::importsObjAndExportsWorldTransformedMeshes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString inputPath = directory.filePath(QStringLiteral("quad.obj"));
    const QByteArray objBytes =
        "  # A quad with negative, relative face indices\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "v 0 1 0\n"
        "f -4 -3 -2 -1\n";

    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    QCOMPARE(input.write(objBytes), qint64(objBytes.size()));
    input.close();

    ProjectIO io;
    const QVariantMap imported = io.importObj(QUrl::fromLocalFile(inputPath));
    QVERIFY2(imported.value(QStringLiteral("ok")).toBool(),
             qPrintable(imported.value(QStringLiteral("error")).toString()));

    const QVariantMap mesh = imported.value(QStringLiteral("mesh")).toMap();
    QCOMPARE(mesh.value(QStringLiteral("shape")).toString(), QStringLiteral("Custom"));
    QCOMPARE(mesh.value(QStringLiteral("vertices")).toList().size(), 4);
    QCOMPARE(mesh.value(QStringLiteral("faces")).toList().size(), 1);

    const QVariantMap object{
        {QStringLiteral("id"), QStringLiteral("object-1")},
        {QStringLiteral("label"), QStringLiteral("Test Quad")},
        {QStringLiteral("shape"), QStringLiteral("Custom")},
        {QStringLiteral("kind"), QStringLiteral("primitive")},
        {QStringLiteral("mesh"), mesh},
        {QStringLiteral("x"), 10.0},
        {QStringLiteral("y"), 0.0},
        {QStringLiteral("z"), 0.0},
        {QStringLiteral("rx"), 0.0},
        {QStringLiteral("ry"), 0.0},
        {QStringLiteral("rz"), 0.0},
        {QStringLiteral("scale"), 2.0},
        {QStringLiteral("scaleX"), 2.0},
        {QStringLiteral("scaleY"), 3.0},
        {QStringLiteral("scaleZ"), 1.0}
    };
    const QVariantMap scene{
        {QStringLiteral("format"), QStringLiteral("qalyvyron.scene")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("objects"), QVariantList{object}}
    };

    const QString outputPath = directory.filePath(QStringLiteral("exported.obj"));
    const QVariantMap exported =
        io.exportObj(QUrl::fromLocalFile(outputPath), scene);
    QVERIFY2(exported.value(QStringLiteral("ok")).toBool(),
             qPrintable(exported.value(QStringLiteral("error")).toString()));
    QCOMPARE(exported.value(QStringLiteral("exportedObjects")).toInt(), 1);
    QCOMPARE(exported.value(QStringLiteral("skippedObjects")).toInt(), 0);

    QFile output(outputPath);
    QVERIFY(output.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray result = output.readAll();
    QVERIFY(result.contains("o Test_Quad"));
    QVERIFY(result.contains("v 10 0 0"));
    QVERIFY(result.contains("v 12 0 0"));
    QVERIFY(result.contains("v 12 3 0"));
    QVERIFY(result.contains("f 1 2 3 4"));
}

void ProjectIOTests::importsAsciiStlAndExportsWorldTransformedBinaryStl()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString inputPath = directory.filePath(QStringLiteral("triangle.stl"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray asciiStl =
        "solid triangle\n"
        "  facet normal 0 0 1\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 1 0 0\n"
        "      vertex 0 1 0\n"
        "    endloop\n"
        "  endfacet\n"
        "endsolid triangle\n";
    QCOMPARE(input.write(asciiStl), qint64(asciiStl.size()));
    input.close();

    ProjectIO io;
    const QVariantMap imported = io.importStl(QUrl::fromLocalFile(inputPath));
    QVERIFY2(imported.value(QStringLiteral("ok")).toBool(),
             qPrintable(imported.value(QStringLiteral("error")).toString()));
    const QVariantMap mesh = imported.value(QStringLiteral("mesh")).toMap();
    QCOMPARE(mesh.value(QStringLiteral("vertices")).toList().size(), 3);
    QCOMPARE(mesh.value(QStringLiteral("faces")).toList().size(), 1);

    // Export bakes object transforms into the standard STL triangle coordinates.
    const QVariantMap object{
        {QStringLiteral("id"), QStringLiteral("object-1")},
        {QStringLiteral("label"), QStringLiteral("Moved Triangle")},
        {QStringLiteral("shape"), QStringLiteral("Custom")},
        {QStringLiteral("kind"), QStringLiteral("primitive")},
        {QStringLiteral("mesh"), mesh},
        {QStringLiteral("x"), 10.0},
        {QStringLiteral("y"), 0.0},
        {QStringLiteral("z"), 0.0},
        {QStringLiteral("rx"), 0.0},
        {QStringLiteral("ry"), 0.0},
        {QStringLiteral("rz"), 0.0},
        {QStringLiteral("scale"), 1.0},
        {QStringLiteral("scaleX"), 2.0},
        {QStringLiteral("scaleY"), 3.0},
        {QStringLiteral("scaleZ"), 1.0},
        {QStringLiteral("visible"), true}
    };
    const QVariantMap document{
        {QStringLiteral("format"), QStringLiteral("qalyvyron.scene")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("objects"), QVariantList{object}}
    };

    const QString outputPath = directory.filePath(QStringLiteral("transformed.stl"));
    const QVariantMap exported = io.exportStl(QUrl::fromLocalFile(outputPath), document);
    QVERIFY2(exported.value(QStringLiteral("ok")).toBool(),
             qPrintable(exported.value(QStringLiteral("error")).toString()));
    QCOMPARE(exported.value(QStringLiteral("exportedTriangles")).toInt(), 1);
    QCOMPARE(QFileInfo(outputPath).size(), qint64(134)); // 84-byte header + one 50-byte triangle.

    // Re-importing the binary file proves the writer and the binary reader agree on the format.
    const QVariantMap roundTrip = io.importStl(QUrl::fromLocalFile(outputPath));
    QVERIFY2(roundTrip.value(QStringLiteral("ok")).toBool(),
             qPrintable(roundTrip.value(QStringLiteral("error")).toString()));
    const QVariantMap roundTripMesh = roundTrip.value(QStringLiteral("mesh")).toMap();
    QCOMPARE(roundTripMesh.value(QStringLiteral("faces")).toList().size(), 1);
    const QVariantList firstVertex =
        roundTripMesh.value(QStringLiteral("vertices")).toList().first().toList();
    QCOMPARE(firstVertex.size(), 3);
    QVERIFY(std::abs(firstVertex.at(0).toDouble() - 10.0) < 1.0e-5);
    const QVariantList secondVertex =
        roundTripMesh.value(QStringLiteral("vertices")).toList().at(1).toList();
    const QVariantList thirdVertex =
        roundTripMesh.value(QStringLiteral("vertices")).toList().at(2).toList();
    QVERIFY(std::abs(secondVertex.at(0).toDouble() - 12.0) < 1.0e-5);
    QVERIFY(std::abs(thirdVertex.at(1).toDouble() - 3.0) < 1.0e-5);
}

void ProjectIOTests::rejectsMalformedStl()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = directory.filePath(QStringLiteral("broken.stl"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray malformed =
        "solid broken\n"
        "facet normal 0 0 1\n"
        "outer loop\n"
        "vertex nope 0 0\n"
        "vertex 1 0 0\n"
        "vertex 0 1 0\n"
        "endloop\n"
        "endfacet\n"
        "endsolid broken\n";
    QCOMPARE(file.write(malformed), qint64(malformed.size()));
    file.close();

    ProjectIO io;
    const QVariantMap result = io.importStl(QUrl::fromLocalFile(path));
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    QVERIFY(!result.value(QStringLiteral("error")).toString().isEmpty());
    QVERIFY(!result.contains(QStringLiteral("mesh")));
}

void ProjectIOTests::rejectsMalformedObjFaces()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = directory.filePath(QStringLiteral("invalid.obj"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray content =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 4\n";
    QCOMPARE(file.write(content), qint64(content.size()));
    file.close();

    ProjectIO io;
    const QVariantMap result = io.importObj(QUrl::fromLocalFile(path));
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    QVERIFY(!result.value(QStringLiteral("error")).toString().isEmpty());
}

void ProjectIOTests::remembersWelcomePreference()
{
    ProjectIO io;
    QSettings settings;
    const QString key = QStringLiteral("interface/showWelcomeOnStartup");
    const bool hadPreviousValue = settings.contains(key);
    const QVariant previousValue = settings.value(key);

    settings.remove(key);
    settings.sync();
    QVERIFY(io.showWelcomeOnStartup());

    io.setShowWelcomeOnStartup(false);
    QVERIFY(!io.showWelcomeOnStartup());
    io.setShowWelcomeOnStartup(true);
    QVERIFY(io.showWelcomeOnStartup());

    if (hadPreviousValue)
        settings.setValue(key, previousValue);
    else
        settings.remove(key);
    settings.sync();
}

void ProjectIOTests::rejectsMalformedJson()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = directory.filePath(QStringLiteral("broken.qvscene"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("{ this is not JSON"), qint64(18));
    file.close();

    ProjectIO io;
    const QVariantMap result = io.loadProject(QUrl::fromLocalFile(path));
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    QVERIFY(!result.value(QStringLiteral("error")).toString().isEmpty());
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QALYVYRON-tests"));
    QCoreApplication::setOrganizationName(QStringLiteral("Tamasrazim"));
    ProjectIOTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_projectio.moc"
