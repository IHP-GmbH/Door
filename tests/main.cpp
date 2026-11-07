#include <QApplication>
#include <QtTest/QtTest>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QStringList>
#include <functional>

#include <cstdio>

#include "tst_libman_gui.h"
#include "tst_libman_viewops.h"
#include "tst_mainwindow_loaders.h"
#include "tst_toolmanager.h"
#include "tst_klayout_requests.h"
#include "tst_oas_writer.h"
#include "tst_snapshot_layout.h"
#include "tst_lstream_writer.h"
#include "tst_libfileparser.h"
#include "tst_libdefine_utils.h"
#include "tst_dialogs.h"
#include "tst_libman_layoutview_create.h"
#include "tst_mainwindow_categories.h"
#include "tst_coverage_expansion.h"
#include "tst_coverage_80.h"
#include "tst_room_path_utils.h"
#include "tst_room_file_lock.h"

namespace
{

/*!********************************************************************************************************
 * \brief Runs one test class and stores its output in a dedicated text file.
 *********************************************************************************************************/
template <typename T>
static int runTestToFile(const QString &fileName, int argc, char **argv)
{
    Q_UNUSED(argc)

    QByteArray fileArg = "-o";
    QByteArray outArg  = QFile::encodeName(fileName) + ",txt";

    char *testArgv[] = {
        argv[0],
        fileArg.data(),
        outArg.data()
    };

    T tc;
    return QTest::qExec(&tc, 3, testArgv);
}

/*!********************************************************************************************************
 * \brief Appends the contents of one file to another file.
 *********************************************************************************************************/
static bool appendFile(QFile &dst, const QString &srcPath)
{
    QFile src(srcPath);
    if(!src.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    dst.write(src.readAll());
    src.close();
    return true;
}

/*!********************************************************************************************************
 * \brief Joins per-suite logs into a single test_results.txt file.
 *********************************************************************************************************/
static void mergeLogs(const QStringList &files, const QString &mergedFile)
{
    QFile out(mergedFile);
    if(!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qDebug() << "Failed to open merged log:" << mergedFile;
        return;
    }

    for(const QString &file : files) {
        out.write("==================================================\n");
        out.write(QFileInfo(file).fileName().toUtf8());
        out.write("\n==================================================\n");

        if(!appendFile(out, file)) {
            out.write("Failed to read file.\n");
        }

        out.write("\n\n");
    }

    out.close();
}

/*!********************************************************************************************************
 * \brief Stores one test entry with a name and a launcher function.
 *********************************************************************************************************/
struct TestEntry
{
    QString                             name;
    std::function<int(const QString &)> run;
};

template <typename T>
static TestEntry makeTestEntry(const QString &name, int argc, char **argv)
{
    TestEntry entry;
    entry.name = name;
    entry.run = [argc, argv](const QString &fileName) -> int {
        return runTestToFile<T>(fileName, argc, argv);
    };
    return entry;
}

} // namespace

#define ADD_TEST(TESTCLASS) makeTestEntry<TESTCLASS>(#TESTCLASS, argc, argv)

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    int status = 0;

    const QString logDir    = QDir::currentPath();
    const QString mergedLog = logDir + "/test_results.txt";

    QFile::remove(mergedLog);

    const QList<TestEntry> tests = {
        ADD_TEST(LibManGui),
        ADD_TEST(LibManViewOpsTest),
        ADD_TEST(MainWindowCategoriesTest),
        ADD_TEST(LibManLayoutViewCreateTest),
        ADD_TEST(MainWindowLoadersTest),
        ADD_TEST(ToolManagerTest),
        ADD_TEST(KLayoutRequestsTest),
        ADD_TEST(OasWriterTest),
        ADD_TEST(SnapshotLayoutPolicyTest),
        ADD_TEST(LStreamWriterTest),
        ADD_TEST(LibFileParserTest),
        ADD_TEST(LibDefineUtilsTest),
        ADD_TEST(DialogsTest),
        ADD_TEST(FormatReadersCoverageTest),
        ADD_TEST(MainWindowCoverageExpansionTest),
        ADD_TEST(MainWindowSlotSmokeTest),
        ADD_TEST(Coverage80MainWindowDeepTest),
        ADD_TEST(Coverage80LStreamReaderTest),
        ADD_TEST(Coverage80GdsExtraTest),
        ADD_TEST(CorePathUtilsTest),
        ADD_TEST(CoreFileLockTest)
    };

    QStringList onlySuites;
    for(int i = 1; i < argc; ++i) {
        onlySuites << QString::fromLocal8Bit(argv[i]);
    }

    QStringList logFiles;

    for(const TestEntry &test : tests) {
        if(!onlySuites.isEmpty() && !onlySuites.contains(test.name)) {
            continue;
        }

        const QString logFile = logDir + "/test_results_" + test.name + ".txt";

        QFile::remove(logFile);

        const int testStatus = test.run(logFile);
        if(testStatus != 0) {
            status += testStatus;
        }
        logFiles << logFile;

        fprintf(stdout,
                "[%s] %s\n",
                testStatus == 0 ? "PASS" : "FAIL",
                test.name.toUtf8().constData());
        fflush(stdout);
    }

    mergeLogs(logFiles, mergedLog);

    if(status == 0) {
        fprintf(stdout, "\n===================================\n");
        fprintf(stdout, "ALL TESTS PASSED\n");
        fprintf(stdout, "===================================\n\n");
        fflush(stdout);
    }
    else {
        fprintf(stdout, "\n===================================\n");
        fprintf(stdout, "TESTS FAILED (status = %d)\n", status);
        fprintf(stdout, "===================================\n\n");
        fflush(stdout);
    }

    fprintf(stdout, "Merged test log: %s\n", mergedLog.toUtf8().constData());
    fflush(stdout);

    return status;
}

#undef ADD_TEST
