#include "projectio.h"

#include <QFile>
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
        {QStringLiteral("scale"), 2.0}
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
    QVERIFY(result.contains("f 1 2 3 4"));
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

QTEST_GUILESS_MAIN(ProjectIOTests)
#include "test_projectio.moc"
