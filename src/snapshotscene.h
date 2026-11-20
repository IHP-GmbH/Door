#ifndef SNAPSHOTSCENE_H
#define SNAPSHOTSCENE_H

#include <QColor>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <QtGlobal>

#include <functional>

struct SnapshotStroke {
    QPolygonF points;
    QColor color;
    bool filled = false;
};

struct SnapshotLabel {
    QPointF pos;
    QString text;
    QColor color;
};

struct SnapshotScene {
    QVector<SnapshotStroke> strokes;
    QVector<SnapshotLabel> labels;
    QRectF bounds;
    bool yUp = true;
    QString message;

    bool empty() const { return strokes.isEmpty() && labels.isEmpty(); }
};

using SnapshotSymbolResolver = std::function<QString(const QString &cellName)>;

SnapshotScene loadCoreSnapshot(const QString &corePath,
                               const QString &viewName,
                               const SnapshotSymbolResolver &symbolPathForCell,
                               const QString &cellName = QString());

/*!
 * From an EmModel ROOM file, resolve topology.layoutPath when it points at a
 * ROOM layout (*.layout.room / *.room). Relative paths are resolved beside the
 * emmodel file. Empty if missing, GDS-only, or unreadable.
 */
QString layoutRoomPathFromEmModel(const QString &emmodelPath, QString *topCellOut = nullptr);

/*! True when path is a ROOM layout suitable for emsetup/layout snapshot. */
bool isRoomLayoutSnapshotPath(const QString &path);

/*! True for GDSII/OASIS paths; the layout snapshot imports them through ROOM. */
bool isForeignLayoutSnapshotPath(const QString &path);

constexpr qint64 kMaxGdsSnapshotBytes = 512LL * 1024 * 1024;
constexpr qint64 kMaxOasisSnapshotBytes = 64LL * 1024 * 1024;

/*! Non-empty message when a GDS/OASIS file is too large to import for a snapshot. */
QString foreignLayoutSnapshotSkipReason(const QString &path, qint64 sizeBytes);
QString foreignLayoutSnapshotSkipReason(const QString &path);

/*!
 * Layout tree selection → focus cell name.
 * Expanded child uses \p treeCellName; view root uses \p resolvedTopCell then \p groupNameFallback.
 */
QString layoutSnapshotFocusCell(bool selectingTreeCell,
                                const QString &treeCellName,
                                const QString &resolvedTopCell,
                                const QString &groupNameFallback);

/*! Stem used when LibMan group name is missing inside *.layout.room (file basename). */
QString layoutSnapshotPathStem(const QString &layoutPath);

/*!
 * Names to try with loadLayoutCellSubtree when LibMan cell ≠ GDS top
 * (requested → path stem → empty/primary). Deduped, order preserved.
 */
QStringList layoutSubtreeLoadAttempts(const QString &requestedCell, const QString &layoutPath);

#endif
