#ifndef TST_SNAPSHOT_LAYOUT_H
#define TST_SNAPSHOT_LAYOUT_H

#include <QObject>

class SnapshotLayoutPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void layoutSnapshotFocusCell_rootUsesResolvedTop();
    void layoutSnapshotFocusCell_rootFallsBackToGroup();
    void layoutSnapshotFocusCell_treeCellWins();
    void layoutSnapshotFocusCell_emptyTreeFallsBack();

    void layoutSubtreeLoadAttempts_mismatchedGroupThenStemThenEmpty();
    void layoutSubtreeLoadAttempts_dedupesIdenticalNames();
    void layoutSnapshotPathStem_stripsLayoutRoomSuffix();

    void isRoomLayoutSnapshotPath_acceptsLayoutRoom();
    void isRoomLayoutSnapshotPath_rejectsEmmodelAndMissing();

    void resolveKLayoutRootCell_emStyleGroupMissingUsesRichestTop();

    void loadCoreSnapshot_layoutGroupNameMissingFallsBackToTop();
    void loadCoreSnapshot_layoutDrawsInstanceArrays();
    void loadCoreSnapshot_layoutTreeCellFramesOnlyThatCell();
};

#endif // TST_SNAPSHOT_LAYOUT_H
