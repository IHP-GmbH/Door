#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QStatusBar>
#include <QTreeWidget>

#include "room/roomcellreader.h"
#include "src/mainwindow.h"

void MainWindow::loadRoomHierarchyAsync(const QString &corePath,
                                        const std::shared_ptr<CoreCacheEntry> &entry,
                                        QTreeWidgetItem *targetItem,
                                        const QString &requestedCellName)
{
    if (!entry || corePath.isEmpty()) {
        return;
    }

    if (entry->loading || entry->loaded) {
        return;
    }

    entry->loading = true;
    statusBar()->showMessage(tr("Sniffing ROOM cell names…"), 0);

    if (targetItem) {
        setLoadingSpinner(targetItem, true);
    }

    auto *watcher = new QFutureWatcher<CoreCacheEntry>(this);

    connect(watcher, &QFutureWatcher<CoreCacheEntry>::finished, this,
            [this, watcher, entry, targetItem, requestedCellName]()
            {
                const CoreCacheEntry r = watcher->result();
                watcher->deleteLater();

                if (targetItem) {
                    setLoadingSpinner(targetItem, false);
                }

                entry->errors = r.errors;
                entry->hierarchy = r.hierarchy;
                entry->loaded = r.loaded;
                entry->loading = false;

                if (!entry->loaded) {
                    for (const QString &e : entry->errors) {
                        error(e, false);
                    }
                    statusBar()->showMessage(tr("ROOM load failed."), 10000);
                    return;
                }

                statusBar()->showMessage(
                    tr("ROOM cells ready: %1").arg(entry->hierarchy.allCells.size()),
                    10000);

                if (!targetItem) {
                    return;
                }

                if (!requestedCellName.isEmpty()) {
                    populateCoreCellChildren(targetItem, entry, requestedCellName);
                    targetItem->setExpanded(true);
                    return;
                }

                populateCoreTopLevel(targetItem, entry);
                targetItem->setExpanded(true);
            });

    auto future = QtConcurrent::run([corePath]() -> CoreCacheEntry {
        CoreCacheEntry out;
        out.path = QFileInfo(corePath).absoluteFilePath();

        RoomCellReader reader(out.path);
        if (!reader.readHierarchy(out.hierarchy)) {
            out.errors = reader.getErrors();
            out.loaded = false;
            return out;
        }

        out.loaded = true;
        return out;
    });

    watcher->setFuture(future);
}
