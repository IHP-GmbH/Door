#include "klayoutCellResolver.h"

#include "room/roomcellreader.h"
#include "room/room_path_utils.h"
#include "gds/gdsreader.h"
#include "oas/oasReader.h"
#include "src/lstreamcellreader.h"

#include <QFileInfo>
#include <QMap>

namespace {

void fillChildCounts(LayoutHierarchySnapshot &out, const QMap<QString, QStringList> &children)
{
    for (auto it = children.constBegin(); it != children.constEnd(); ++it) {
        out.childCounts.insert(it.key(), it.value().size());
    }
}

void fillChildCounts(LayoutHierarchySnapshot &out, const QHash<QString, QStringList> &children)
{
    for (auto it = children.constBegin(); it != children.constEnd(); ++it) {
        out.childCounts.insert(it.key(), it.value().size());
    }
}

LayoutHierarchySnapshot snapshotFromGds(const GdsReader::GdsHierarchy &h)
{
    LayoutHierarchySnapshot out;
    out.topCells = h.topCells;
    out.allCells = h.allCells;
    fillChildCounts(out, h.children);
    return out;
}

LayoutHierarchySnapshot snapshotFromOas(const LayoutHierarchy &h)
{
    LayoutHierarchySnapshot out;
    out.topCells = h.topCells;
    out.allCells = h.allCells;
    fillChildCounts(out, h.children);
    return out;
}

LayoutHierarchySnapshot snapshotFromCore(const RoomCellReader::CoreHierarchy &h)
{
    LayoutHierarchySnapshot out;
    out.topCells = h.topCells;
    out.allCells = h.allCells;
    fillChildCounts(out, h.children);
    return out;
}

LayoutHierarchySnapshot snapshotFromLStream(const QStringList &cellNames)
{
    LayoutHierarchySnapshot out;
    out.topCells = cellNames;
    out.topCells.sort();
    for (const QString &name : cellNames) {
        out.allCells.insert(name);
    }
    return out;
}

bool isLayoutCorePath(const QString &path)
{
    const RoomViewIdentity identity = parseRoomViewIdentity(path);
    if (identity.valid) {
        return isLayoutRoomViewName(identity.viewName);
    }

    return QFileInfo(path).suffix().compare(QStringLiteral("core"), Qt::CaseInsensitive) == 0;
}

QString findCellIgnoreCase(const QSet<QString> &cells, const QString &name)
{
    if (name.isEmpty()) {
        return QString();
    }
    if (cells.contains(name)) {
        return name;
    }
    for (const QString &cell : cells) {
        if (cell.compare(name, Qt::CaseInsensitive) == 0) {
            return cell;
        }
    }
    return QString();
}

QString pickRichestTopCell(const LayoutHierarchySnapshot &hierarchy)
{
    if (hierarchy.topCells.isEmpty()) {
        return QString();
    }

    QString best = hierarchy.topCells.first();
    int bestScore = hierarchy.childCounts.value(best, 0);

    for (int i = 1; i < hierarchy.topCells.size(); ++i) {
        const QString &candidate = hierarchy.topCells.at(i);
        const int score = hierarchy.childCounts.value(candidate, 0);
        if (score > bestScore
            || (score == bestScore && candidate < best)) {
            best = candidate;
            bestScore = score;
        }
    }

    return best;
}

} // namespace

QString resolveKLayoutRootCell(const LayoutHierarchySnapshot &hierarchy,
                               const QString &groupName,
                               const QString &hintName)
{
    if (const QString matched = findCellIgnoreCase(hierarchy.allCells, groupName);
        !matched.isEmpty()) {
        return matched;
    }

    if (const QString matched = findCellIgnoreCase(hierarchy.allCells, hintName);
        !matched.isEmpty()) {
        return matched;
    }

    if (hierarchy.topCells.size() == 1) {
        return hierarchy.topCells.first();
    }

    if (!hierarchy.topCells.isEmpty()) {
        return pickRichestTopCell(hierarchy);
    }

    return QString();
}

bool loadLayoutHierarchySnapshot(const QString &layoutPath,
                                 LayoutHierarchySnapshot &out,
                                 QStringList *errors)
{
    const QFileInfo fi(layoutPath);
    if (!fi.exists() || !fi.isFile()) {
        if (errors) {
            *errors << QStringLiteral("Layout file not found: %1").arg(layoutPath);
        }
        return false;
    }

    const QString suffix = fi.suffix().toLower();

    if (isLayoutCorePath(layoutPath)) {
        RoomCellReader reader(layoutPath);
        RoomCellReader::CoreHierarchy hierarchy;
        if (!reader.readHierarchy(hierarchy)) {
            if (errors) {
                *errors = reader.getErrors();
            }
            return false;
        }
        out = snapshotFromCore(hierarchy);
        return !out.allCells.isEmpty() || !out.topCells.isEmpty();
    }

    if (suffix == QLatin1String("gds")) {
        GdsReader reader(layoutPath);
        GdsReader::GdsHierarchy hierarchy;
        if (!reader.readHierarchy(hierarchy)) {
            if (errors) {
                *errors = reader.getErrors();
            }
            return false;
        }
        out = snapshotFromGds(hierarchy);
        return !out.allCells.isEmpty() || !out.topCells.isEmpty();
    }

    if (suffix == QLatin1String("oas")) {
        oasReader reader(layoutPath);
        LayoutHierarchy hierarchy;
        if (!reader.readHierarchy(hierarchy)) {
            if (errors) {
                *errors = reader.getErrors();
            }
            return false;
        }
        out = snapshotFromOas(hierarchy);
        return !out.allCells.isEmpty() || !out.topCells.isEmpty();
    }

    if (suffix == QLatin1String("lstr")) {
        const LStreamCellReader::Result result = LStreamCellReader::read(layoutPath);
        if (!result.loaded) {
            if (errors) {
                *errors = result.errors;
            }
            return false;
        }
        out = snapshotFromLStream(result.cellNames);
        return !out.allCells.isEmpty();
    }

    if (errors) {
        *errors << QStringLiteral("Unsupported layout file type: %1").arg(layoutPath);
    }
    return false;
}
