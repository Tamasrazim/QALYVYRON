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
