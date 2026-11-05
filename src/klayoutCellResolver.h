#ifndef KLAYOUT_CELL_RESOLVER_H
#define KLAYOUT_CELL_RESOLVER_H

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

/*!
 * \brief Minimal hierarchy snapshot used to pick a KLayout root cell.
 */
struct LayoutHierarchySnapshot
{
    QStringList        topCells;
    QSet<QString>      allCells;
    QHash<QString, int> childCounts; /*!< Direct child ref counts for top-cell scoring. */
};

/*!
 * \param groupName Library / cell-view group name (preferred when present in the file).
 * \param hintName  Optional extra name hint (e.g. layout file stem).
 */
QString resolveKLayoutRootCell(const LayoutHierarchySnapshot &hierarchy,
                               const QString &groupName,
                               const QString &hintName = QString());

bool loadLayoutHierarchySnapshot(const QString &layoutPath,
                                 LayoutHierarchySnapshot &out,
                                 QStringList *errors = nullptr);

#endif // KLAYOUT_CELL_RESOLVER_H
