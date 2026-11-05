#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#define private public
#include "src/klayout_tools.h"
#include "src/mainwindow.h"
#include "tst_klayout_requests.h"
#undef private

#include "src/klayoutCellResolver.h"

/*!*********************************************************************************************************************
 * \brief Verifies that sendKLayoutOpenRequest() fails when command file path is empty.
 **********************************************************************************************************************/
void KLayoutRequestsTest::sendKLayoutOpenRequest_emptyCommandFile_returnsFalse()
{
    MainWindow w(QString(), QDir::currentPath());
    w.m_klayoutTools.m_cmdFile.clear();

    QVERIFY(!w.sendKLayoutOpenRequest("dummy.gds", "TOP"));
}

/*!*********************************************************************************************************************
 * \brief Verifies that sendKLayoutOpenRequest() writes correct JSON command file.
 **********************************************************************************************************************/
void KLayoutRequestsTest::sendKLayoutOpenRequest_writesJsonCommand()
{
    MainWindow w(QString(), QDir::currentPath());

    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "Failed to create temporary directory");

    const QString gdsPath = dir.filePath("sample.gds");
    {
        QFile f(gdsPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("dummy");
        f.close();
    }

    const QString cmdFile = dir.filePath("klayout_cmd.json");
    w.m_klayoutTools.m_cmdFile = cmdFile;

    QVERIFY(w.sendKLayoutOpenRequest(gdsPath, "TOP"));

    QFile f(cmdFile);
    QVERIFY2(f.exists(), "Command file was not created");
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));

    const QByteArray data = f.readAll();
    f.close();

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    QVERIFY2(doc.isObject(), "Command file does not contain valid JSON object");

    const QJsonObject obj = doc.object();

    QCOMPARE(obj.value("action").toString(), QString("open"));
    QCOMPARE(obj.value("cell").toString(), QString("TOP"));
    QCOMPARE(obj.value("file").toString(), QFileInfo(gdsPath).absoluteFilePath());
}

/*!*********************************************************************************************************************
 * \brief Verifies that sendKLayoutSelectRequest() fails when command file path is empty.
 **********************************************************************************************************************/
void KLayoutRequestsTest::sendKLayoutSelectRequest_emptyCommandFile_returnsFalse()
{
    MainWindow w(QString(), QDir::currentPath());
    w.m_klayoutTools.m_cmdFile.clear();

    QVERIFY(!w.sendKLayoutSelectRequest("dummy.gds", "TOP"));
}

/*!*********************************************************************************************************************
 * \brief Verifies that sendKLayoutSelectRequest() writes correct JSON command file.
 **********************************************************************************************************************/
void KLayoutRequestsTest::sendKLayoutSelectRequest_writesJsonCommand()
{
    MainWindow w(QString(), QDir::currentPath());

    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "Failed to create temporary directory");

    const QString gdsPath = dir.filePath("sample.gds");
    {
        QFile f(gdsPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("dummy");
        f.close();
    }

    const QString cmdFile = dir.filePath("klayout_cmd.json");
    w.m_klayoutTools.m_cmdFile = cmdFile;

    QVERIFY(w.sendKLayoutSelectRequest(gdsPath, "MY_CELL"));

    QFile f(cmdFile);
    QVERIFY2(f.exists(), "Command file was not created");
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));

    const QByteArray data = f.readAll();
    f.close();

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    QVERIFY2(doc.isObject(), "Command file does not contain valid JSON object");

    const QJsonObject obj = doc.object();

    QCOMPARE(obj.value("action").toString(), QString("select"));
    QCOMPARE(obj.value("cell").toString(), QString("MY_CELL"));
    QCOMPARE(obj.value("file").toString(), QFileInfo(gdsPath).absoluteFilePath());
}

/*!*********************************************************************************************************************
 * \brief Verifies that createServerScript() creates a Python file with expected content.
 **********************************************************************************************************************/
void KLayoutRequestsTest::createKLayoutServerScript_createsPythonFile()
{
    MainWindow w(QString(), QDir::currentPath());

    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "Failed to create temporary directory");

    const QString cmdFile = dir.filePath("server_cmd.json");
    const QString scriptPath = w.m_klayoutTools.createServerScript(cmdFile);

    QVERIFY2(!scriptPath.isEmpty(), "Script path is empty");
    QVERIFY2(QFileInfo(scriptPath).exists(), "Script file was not created");

    QFile f(scriptPath);
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to open generated script");

    const QString text = QString::fromUtf8(f.readAll());
    f.close();

    QVERIFY(text.contains("CMD_FILE = "));
    QVERIFY(text.contains("_handle"));
    QVERIFY(text.contains("_poll"));
    QVERIFY(text.contains("\"open\"") || text.contains("open"));
    QVERIFY(text.contains("\"select\"") || text.contains("select"));
    QVERIFY(text.contains("libman_klayout_server") ||
            text.contains("QTimer") ||
            text.contains("load_layout"));

    QFile::remove(scriptPath);
}

void KLayoutRequestsTest::resolveKLayoutRootCell_prefersGroupWhenPresent()
{
    LayoutHierarchySnapshot hierarchy;
    hierarchy.topCells = QStringList{
        QStringLiteral("alpha"), QStringLiteral("beta")};
    hierarchy.allCells.insert(QStringLiteral("alpha"));
    hierarchy.allCells.insert(QStringLiteral("beta"));
    hierarchy.allCells.insert(QStringLiteral("my_group"));

    QCOMPARE(resolveKLayoutRootCell(hierarchy, QStringLiteral("my_group")),
             QStringLiteral("my_group"));
}

void KLayoutRequestsTest::resolveKLayoutRootCell_singleTopCell()
{
    LayoutHierarchySnapshot hierarchy;
    hierarchy.topCells = QStringList{QStringLiteral("TOP")};
    hierarchy.allCells.insert(QStringLiteral("TOP"));
    hierarchy.allCells.insert(QStringLiteral("child"));

    QCOMPARE(resolveKLayoutRootCell(hierarchy, QStringLiteral("missing_group")),
             QStringLiteral("TOP"));
}

void KLayoutRequestsTest::resolveKLayoutRootCell_multipleTopCellsPrefersRichest()
{
    LayoutHierarchySnapshot hierarchy;
    hierarchy.topCells = QStringList{
        QStringLiteral("zebra"), QStringLiteral("alpha")};
    hierarchy.topCells.sort();
    hierarchy.allCells.insert(QStringLiteral("zebra"));
    hierarchy.allCells.insert(QStringLiteral("alpha"));
    hierarchy.childCounts.insert(QStringLiteral("zebra"), 12);
    hierarchy.childCounts.insert(QStringLiteral("alpha"), 1);

    QCOMPARE(resolveKLayoutRootCell(hierarchy, QStringLiteral("not_in_file")),
             QStringLiteral("zebra"));
}

void KLayoutRequestsTest::resolveKLayoutRootCell_prefersFileStemHint()
{
    LayoutHierarchySnapshot hierarchy;
    hierarchy.topCells = QStringList{
        QStringLiteral("alpha"), QStringLiteral("design_top")};
    hierarchy.topCells.sort();
    hierarchy.allCells.insert(QStringLiteral("alpha"));
    hierarchy.allCells.insert(QStringLiteral("design_top"));
    hierarchy.childCounts.insert(QStringLiteral("alpha"), 50);
    hierarchy.childCounts.insert(QStringLiteral("design_top"), 1);

    QCOMPARE(resolveKLayoutRootCell(hierarchy,
                                    QStringLiteral("missing_group"),
                                    QStringLiteral("design_top")),
             QStringLiteral("design_top"));
}

void KLayoutRequestsTest::resolveKLayoutRootCell_emptyHierarchyReturnsEmpty()
{
    LayoutHierarchySnapshot hierarchy;
    QVERIFY(resolveKLayoutRootCell(hierarchy, QStringLiteral("group")).isEmpty());
}

void KLayoutRequestsTest::loadLayoutHierarchySnapshot_layoutCore_fixture()
{
    const QString fixture =
        QDir(QDir::currentPath()).filePath(QStringLiteral("data/sg13g2_stdcell/sg13g2_stdcell/sg13g2_stdcell.layout.room"));
    if (!QFileInfo::exists(fixture)) {
        QSKIP("sg13g2_stdcell.layout.room fixture not found");
    }

    LayoutHierarchySnapshot hierarchy;
    QStringList errors;
    QVERIFY2(loadLayoutHierarchySnapshot(fixture, hierarchy, &errors),
             qPrintable(errors.join(QLatin1Char(';'))));
    QVERIFY(hierarchy.topCells.size() > 1);
    QVERIFY(!hierarchy.allCells.contains(QStringLiteral("sg13g2_stdcell")));

    const QString resolved =
        resolveKLayoutRootCell(hierarchy, QStringLiteral("sg13g2_stdcell"));
    QVERIFY(hierarchy.topCells.contains(resolved));
}
