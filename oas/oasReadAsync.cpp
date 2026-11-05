#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QMovie>
#include <QPointer>
#include <QTreeWidget>
#include <QPainter>
#include <QPixmap>
#include <QPalette>
#include <QtMath>
#include <QDebug>
#include <QMutex>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QStatusBar>
#include <QFileInfo>
#include <QPair>
#include <QVector>


#include "oas/oasReader.h"
#include "src/mainwindow.h"
#include "ui_mainwindow.h"

/*!*******************************************************************************************************************
 * \brief Returns a hexadecimal UTF-16 dump of the given string.
 *
 * Converts each QChar of the input string into its Unicode code point
 * representation (U+XXXX format) and joins them into a single string.
 *
 * \param s Input string to inspect.
 * \return Space-separated UTF-16 code points.
 *********************************************************************************************************************/
/*static QString dumpU16(const QString& s)
{
    QStringList parts;
    parts.reserve(s.size());
    for (QChar ch : s)
        parts << QString("U+%1").arg(ch.unicode(), 4, 16, QLatin1Char('0'));
    return parts.join(' ');
}*/

/*!*******************************************************************************************************************
 * \brief Prints detailed debug information about a QString.
 *
 * Outputs the plain text, UTF-16 code points and UTF-8 hexadecimal
 * representation of the given string using qDebug().
 *
 * \param tag Debug label to prefix the output.
 * \param s   String to inspect.
 *********************************************************************************************************************/
/*static void dbgStr(const char* tag, const QString& s)
{
    qDebug().noquote()
    << tag
    << "text='" << s << "'"
    << "u16=[" << dumpU16(s) << "]"
    << "utf8hex=" << QString(s.toUtf8().toHex(' '));
}*/

/*!*******************************************************************************************************************
 * \brief Ensures that an OAS cache entry exists for the given file path.
 *
 * If the OASIS file is already cached, the existing entry is returned.
 * Otherwise, a new cache entry is created, stored and returned.
 *
 * \param oasPath Path to the OASIS file.
 * \return Shared pointer to the corresponding cache entry.
 *********************************************************************************************************************/
std::shared_ptr<MainWindow::OasCacheEntry> MainWindow::ensureOasLoaded(const QString &oasPath)
{
    const QString key = QFileInfo(oasPath).absoluteFilePath();

    auto it = m_oasCache.find(key);
    if (it != m_oasCache.end()) {
        return it.value();
    }

    auto entry = std::make_shared<OasCacheEntry>();
    entry->path = key;

    m_oasCache.insert(key, entry);
    return entry;
}

/*!*******************************************************************************************************************
 * \brief Populates OASIS cells in the tree widget.
 *
 * Lists every cell from the hierarchy (not only top cells). Top cells are
 * inserted first, then the remaining names alphabetically. Child indicators
 * are added when the cell has placements.
 *
 * \param oasItem Tree widget item representing the OASIS file.
 * \param entry   Shared cache entry containing the parsed hierarchy.
 *********************************************************************************************************************/
void MainWindow::populateOasTopLevel(QTreeWidgetItem *oasItem,
                                     const std::shared_ptr<OasCacheEntry> &entry)
{
    if (!oasItem || !entry) {
        return;
    }

    if (oasItem->childCount() > 0) {
        return;
    }

    QStringList names;
    names.reserve(entry->hierarchy.allCells.size());

    QSet<QString> seen;
    for (const QString &topCell : entry->hierarchy.topCells) {
        if (seen.contains(topCell)) {
            continue;
        }
        names << topCell;
        seen.insert(topCell);
    }

    QStringList rest;
    rest.reserve(entry->hierarchy.allCells.size());
    for (const QString &cell : entry->hierarchy.allCells) {
        if (!seen.contains(cell)) {
            rest << cell;
        }
    }
    rest.sort();
    names += rest;

    for (const QString &cellName : names) {
        auto *cellItem = new QTreeWidgetItem(oasItem);
        cellItem->setText(0, cellName);
        cellItem->setData(0, RoleType, ItemCell);
        cellItem->setData(0, RoleCellName, cellName);
        cellItem->setData(0, RoleOasPath, entry->path);

        const auto it = entry->hierarchy.children.find(cellName);
        if (it != entry->hierarchy.children.end() && !it.value().isEmpty()) {
            cellItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        }
    }
}

/*!*******************************************************************************************************************
 * \brief Populates the child cells of a specific OASIS cell.
 *
 * Creates tree items for all direct child cells of the given parent cell.
 * Child indicators are added if further hierarchy levels exist.
 *
 * \param cellItem Tree widget item representing the parent cell.
 * \param entry    Shared cache entry containing the parsed hierarchy.
 * \param cellName Name of the parent cell.
 *********************************************************************************************************************/
void MainWindow::populateOasCellChildren(QTreeWidgetItem *cellItem,
                                         const std::shared_ptr<OasCacheEntry> &entry,
                                         const QString &cellName)
{
    if (!cellItem || !entry) {
        return;
    }

    if (cellItem->childCount() > 0) {
        return;
    }

    const auto it = entry->hierarchy.children.find(cellName);
    if (it == entry->hierarchy.children.end()) {
        return;
    }

    const QStringList childs = it.value();
    for (const QString &ch : childs) {

        //dbgStr("OAS CHILD", ch);

        auto *chItem = new QTreeWidgetItem(cellItem);
        chItem->setText(0, ch);
        chItem->setData(0, RoleType, ItemCell);
        chItem->setData(0, RoleCellName, ch);
        chItem->setData(0, RoleOasPath, entry->path);

        const auto it2 = entry->hierarchy.children.find(ch);
        if (it2 != entry->hierarchy.children.end() && !it2.value().isEmpty()) {
            chItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        }
    }
}

/*!*******************************************************************************************************************
 * \brief Asynchronously loads and parses an OASIS hierarchy.
 *
 * Runs the OASIS parser in a background thread and updates the UI
 * once loading is complete. Displays status messages and optionally
 * expands a requested cell in the hierarchy.
 *
 * \param oasPath            Path to the OASIS file.
 * \param entry              Shared cache entry to populate.
 * \param targetItem         Tree widget item representing the OASIS node.
 * \param requestedCellName  Optional cell name to expand after loading.
 *********************************************************************************************************************/
void MainWindow::loadOasHierarchyAsync(const QString &oasPath,
                                       const std::shared_ptr<OasCacheEntry> &entry,
                                       QTreeWidgetItem *targetItem,
                                       const QString &requestedCellName /* = QString() */)
{
    if (!entry || oasPath.isEmpty()) {
        return;
    }

    if (entry->loading || entry->loaded) {
        return;
    }

    entry->loading = true;
    statusBar()->showMessage("Scanning OASIS hierarchy…", 0);

    if (targetItem) {
        setLoadingSpinner(targetItem, true);
    }

    const bool progressiveRoot = targetItem && requestedCellName.isEmpty();
    QTreeWidgetItem *targetRaw = targetItem;
    QPointer<MainWindow> self(this);
    const QString absPath = QFileInfo(oasPath).absoluteFilePath();

    auto *watcher = new QFutureWatcher<OasCacheEntry>(this);

    connect(watcher, &QFutureWatcher<OasCacheEntry>::finished, this,
            [this, watcher, entry, targetRaw, requestedCellName]()
            {
                const OasCacheEntry r = watcher->result();
                watcher->deleteLater();

                if (targetRaw) {
                    setLoadingSpinner(targetRaw, false);
                }

                entry->errors    = r.errors;
                entry->hierarchy = r.hierarchy;
                entry->loaded    = r.loaded;
                entry->loading   = false;

                if (!entry->loaded) {
                    for (const QString &e : entry->errors) {
                        error(e, false);
                    }
                    statusBar()->showMessage("OASIS load failed.", 10000);
                    return;
                }

                statusBar()->showMessage(
                    QString("OASIS loaded: %1 cells").arg(entry->hierarchy.allCells.size()),
                    10000
                    );

                if (!targetRaw || !targetRaw->treeWidget()) {
                    return;
                }

                clearTreeItemChildren(targetRaw);

                if (!requestedCellName.isEmpty()) {
                    populateOasCellChildren(targetRaw, entry, requestedCellName);
                    targetRaw->setExpanded(true);
                    return;
                }

                populateOasTopLevel(targetRaw, entry);
                targetRaw->setExpanded(true);
            });

    auto future = QtConcurrent::run([absPath, self, targetRaw, progressiveRoot]() -> OasCacheEntry {
        OasCacheEntry out;
        out.path = absPath;

        if (self) {
            QMetaObject::invokeMethod(self.data(), [self]() {
                if (!self) {
                    return;
                }
                self->statusBar()->showMessage(
                    QStringLiteral("Reading OASIS file into memory…"), 0);
            }, Qt::QueuedConnection);
        }

        QMutex batchMutex;
        QStringList pendingAdds;
        QVector<QPair<QString, QString>> pendingRenames;
        QElapsedTimer lastFlush;
        lastFlush.start();
        int reported = 0;
        int renamed = 0;

        auto flushProgress = [&](bool force) {
            if (!progressiveRoot || !self || !targetRaw) {
                pendingAdds.clear();
                pendingRenames.clear();
                return;
            }

            QStringList addBatch;
            QVector<QPair<QString, QString>> renameBatch;
            {
                QMutexLocker lock(&batchMutex);
                // Flush often so wafer-sized OAS shows cells within the first seconds.
                if (!force
                    && pendingAdds.size() < 16
                    && pendingRenames.size() < 16
                    && lastFlush.elapsed() < 80) {
                    return;
                }
                addBatch.swap(pendingAdds);
                renameBatch.swap(pendingRenames);
                lastFlush.restart();
            }
            if (addBatch.isEmpty() && renameBatch.isEmpty()) {
                return;
            }

            reported += addBatch.size();
            renamed += renameBatch.size();
            const int total = reported;
            const int renamedTotal = renamed;
            QMetaObject::invokeMethod(self.data(),
                [self, targetRaw, absPath, addBatch, renameBatch, total, renamedTotal]() {
                if (!self || !targetRaw || !targetRaw->treeWidget()) {
                    return;
                }
                if (!addBatch.isEmpty()) {
                    self->appendProgressCellNames(targetRaw, addBatch,
                                                  MainWindow::RoleOasPath, absPath);
                }
                if (!renameBatch.isEmpty()) {
                    self->renameProgressCellNames(targetRaw, renameBatch);
                }
                self->statusBar()->showMessage(
                    QStringLiteral("Scanning OASIS… %1 cells (%2 named)")
                        .arg(total)
                        .arg(renamedTotal),
                    0);
            }, Qt::QueuedConnection);
        };

        oasReader reader(out.path);
        if (!reader.readHierarchy(out.hierarchy,
                [&](const QString &cellName) {
                    // Skip unresolved OAS ref stubs — real names arrive via onRename.
                    if (cellName.startsWith(QLatin1String("#oasref:"))) {
                        return;
                    }
                    {
                        QMutexLocker lock(&batchMutex);
                        pendingAdds << cellName;
                    }
                    flushProgress(false);
                },
                [&](const QString &from, const QString &to) {
                    {
                        QMutexLocker lock(&batchMutex);
                        if (from.startsWith(QLatin1String("#oasref:"))) {
                            // Stub was never shown; publish the resolved name as an add.
                            pendingAdds << to;
                        } else {
                            pendingRenames.append(qMakePair(from, to));
                        }
                    }
                    flushProgress(false);
                })) {
            out.errors = reader.getErrors();
            out.loaded = false;
            return out;
        }

        flushProgress(true);
        out.loaded = true;
        return out;
    });

    watcher->setFuture(future);
}
