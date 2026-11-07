#include <QtTest/QtTest>

#include <QTemporaryDir>
#include <QFile>

#include "src/snapshotscene.h"
#include "src/klayoutCellResolver.h"
#include "tst_snapshot_layout.h"

#ifndef LIBMAN_NO_ROOM
#include "database.h"

namespace {

room::Shape snapshotRect(std::int64_t llx, std::int64_t lly, std::int64_t urx, std::int64_t ury)
{
    room::Shape::RectData data;
    data.box = room::Box(llx, lly, urx, ury);
    data.layerId = 1;
    return room::Shape(data);
}

// TOP: one frame rect + CHILD placed as a 3x2 array. CHILD: one 100x100 rect.
QString writeArrayFixture(const QTemporaryDir &dir)
{
    room::Database db;
    db.setFileView(room::ViewType::Layout);
    room::Lib &lib = db.lib();

    room::CellContent &child = lib.getOrCreateCell("CHILD").getOrCreateContent(room::ViewType::Layout);
    child.block().shapes().push_back(snapshotRect(0, 0, 100, 100));

    room::CellContent &top = lib.getOrCreateCell("TOP").getOrCreateContent(room::ViewType::Layout);
    top.block().shapes().push_back(snapshotRect(0, 0, 2000, 2000));
    room::Instance array("CHILD", room::Transform(500, 500));
    array.setArray(3, 2, 300, 300);
    top.block().instances().push_back(array);

    lib.recomputeAllBBoxes();
    lib.refreshIndex(room::ViewType::Layout);
    const QString path = dir.filePath(QStringLiteral("cmim_like.layout.room"));
    db.saveToFile(path.toStdString(), room::ViewType::Layout);
    return path;
}

} // namespace
#endif

void SnapshotLayoutPolicyTest::layoutSnapshotFocusCell_rootUsesResolvedTop()
{
    QCOMPARE(layoutSnapshotFocusCell(false, QString(), QStringLiteral("TOP"),
                                     QStringLiteral("wafer")),
             QStringLiteral("TOP"));
}

void SnapshotLayoutPolicyTest::layoutSnapshotFocusCell_rootFallsBackToGroup()
{
    QCOMPARE(layoutSnapshotFocusCell(false, QString(), QString(), QStringLiteral("wafer")),
             QStringLiteral("wafer"));
}

void SnapshotLayoutPolicyTest::layoutSnapshotFocusCell_treeCellWins()
{
    QCOMPARE(layoutSnapshotFocusCell(true, QStringLiteral("DIE_3"), QStringLiteral("TOP"),
                                     QStringLiteral("wafer")),
             QStringLiteral("DIE_3"));
}

void SnapshotLayoutPolicyTest::layoutSnapshotFocusCell_emptyTreeFallsBack()
{
    QCOMPARE(layoutSnapshotFocusCell(true, QString(), QStringLiteral("TOP"),
                                     QStringLiteral("wafer")),
             QStringLiteral("TOP"));
}

void SnapshotLayoutPolicyTest::layoutSubtreeLoadAttempts_mismatchedGroupThenStemThenEmpty()
{
    const QStringList attempts = layoutSubtreeLoadAttempts(
        QStringLiteral("cmim_2u3_tm1fix"),
        QStringLiteral("C:/lib/cmim_2u3_tm1fix/cmim_2u3_tm1fix.layout.room"));

    QCOMPARE(attempts.size(), 2); // requested == stem, then empty
    QCOMPARE(attempts.at(0), QStringLiteral("cmim_2u3_tm1fix"));
    QVERIFY(attempts.at(1).isEmpty());
}

void SnapshotLayoutPolicyTest::layoutSubtreeLoadAttempts_dedupesIdenticalNames()
{
    const QStringList attempts = layoutSubtreeLoadAttempts(
        QStringLiteral("TOP"),
        QStringLiteral("/tmp/chip.layout.room"));

    QCOMPARE(attempts.size(), 3);
    QCOMPARE(attempts.at(0), QStringLiteral("TOP"));
    QCOMPARE(attempts.at(1), QStringLiteral("chip"));
    QVERIFY(attempts.at(2).isEmpty());
}

void SnapshotLayoutPolicyTest::layoutSnapshotPathStem_stripsLayoutRoomSuffix()
{
    QCOMPARE(layoutSnapshotPathStem(QStringLiteral("a/b/cmim_2u3_tm1fix.layout.room")),
             QStringLiteral("cmim_2u3_tm1fix"));
    QCOMPARE(layoutSnapshotPathStem(QStringLiteral("wafer.layout.room")),
             QStringLiteral("wafer"));
}

void SnapshotLayoutPolicyTest::isRoomLayoutSnapshotPath_acceptsLayoutRoom()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString layout = dir.filePath(QStringLiteral("cell.layout.room"));
    {
        QFile f(layout);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("room");
    }
    QVERIFY(isRoomLayoutSnapshotPath(layout));
}

void SnapshotLayoutPolicyTest::isRoomLayoutSnapshotPath_rejectsEmmodelAndMissing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString em = dir.filePath(QStringLiteral("cell.emmodel.room"));
    {
        QFile f(em);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("em");
    }
    QVERIFY(!isRoomLayoutSnapshotPath(em));
    QVERIFY(!isRoomLayoutSnapshotPath(dir.filePath(QStringLiteral("missing.layout.room"))));
    QVERIFY(!isRoomLayoutSnapshotPath(QString()));
}

void SnapshotLayoutPolicyTest::resolveKLayoutRootCell_emStyleGroupMissingUsesRichestTop()
{
    // EM/LibMan group name often differs from GDS tops inside *.layout.room.
    LayoutHierarchySnapshot hierarchy;
    hierarchy.topCells << QStringLiteral("TOP") << QStringLiteral("PAD");
    hierarchy.allCells.insert(QStringLiteral("TOP"));
    hierarchy.allCells.insert(QStringLiteral("PAD"));
    hierarchy.allCells.insert(QStringLiteral("VIA"));
    hierarchy.childCounts.insert(QStringLiteral("TOP"), 12);
    hierarchy.childCounts.insert(QStringLiteral("PAD"), 1);

    QCOMPARE(resolveKLayoutRootCell(hierarchy,
                                    QStringLiteral("cmim_2u3_tm1fix"),
                                    QStringLiteral("cmim_2u3_tm1fix")),
             QStringLiteral("TOP"));
}

void SnapshotLayoutPolicyTest::loadCoreSnapshot_layoutGroupNameMissingFallsBackToTop()
{
#ifdef LIBMAN_NO_ROOM
    QSKIP("needs ROOM build");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeArrayFixture(dir);

    // EM regression: LibMan group name is not a cell inside *.layout.room.
    const SnapshotScene scene =
        loadCoreSnapshot(path, QStringLiteral("layout"), {}, QStringLiteral("cmim_2u3_tm1fix"));
    QVERIFY2(!scene.empty(), qPrintable(scene.message));
    QVERIFY(!scene.message.contains(QStringLiteral("not found")));
#endif
}

void SnapshotLayoutPolicyTest::loadCoreSnapshot_layoutDrawsInstanceArrays()
{
#ifdef LIBMAN_NO_ROOM
    QSKIP("needs ROOM build");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeArrayFixture(dir);

    const SnapshotScene scene = loadCoreSnapshot(path, QStringLiteral("layout"), {}, QStringLiteral("TOP"));
    QVERIFY2(!scene.empty(), qPrintable(scene.message));
    // Frame + every array element (3x2), not just the first placement.
    QVERIFY2(scene.strokes.size() >= 1 + 6,
             qPrintable(QStringLiteral("strokes=%1").arg(scene.strokes.size())));
    QVERIFY(scene.bounds.contains(QRectF(0, 0, 2000, 2000)));
#endif
}

void SnapshotLayoutPolicyTest::loadCoreSnapshot_layoutTreeCellFramesOnlyThatCell()
{
#ifdef LIBMAN_NO_ROOM
    QSKIP("needs ROOM build");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeArrayFixture(dir);

    // Expanded tree item: snapshot shows CHILD alone, not the TOP frame.
    const SnapshotScene scene = loadCoreSnapshot(path, QStringLiteral("layout"), {}, QStringLiteral("CHILD"));
    QVERIFY2(!scene.empty(), qPrintable(scene.message));
    QCOMPARE(scene.strokes.size(), 1);
    QVERIFY(scene.bounds.width() <= 100.0 + 1e-6);
    QVERIFY(scene.bounds.height() <= 100.0 + 1e-6);
#endif
}
