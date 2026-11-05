#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QMovie>
#include <QStatusBar>
#include <QPointer>
#include <QTreeWidget>
#include <QPainter>
#include <QPixmap>
#include <QPalette>
#include <QtMath>
#include <QMutex>
#include <QElapsedTimer>
#include <QMetaObject>


#include "gds/gdsreader.h"
#include "src/mainwindow.h"
#include "src/mainwindow.h"

/*!********************************************************************************************************************
 * \brief Creates a spinner icon pixmap for loading indication.
 *
 * Renders a small animated spinner icon using QPainter. The spinner color is derived
 * from the widget palette to match light/dark UI themes. The animation frame is defined
 * by the rotation angle.
 *
 * The icon is intended for use in item views (e.g. QTreeWidget) to indicate background
 * loading activity.
 *
 * \param w Widget used to obtain palette colors (may be nullptr).
 * \param angleDeg Rotation angle in degrees for the current animation frame.
 * \param sizePx Icon size in pixels (width = height).
 * \return Generated spinner icon.
 *********************************************************************************************************************/
static QIcon makeSpinnerIcon(const QWidget *w, int angleDeg, int sizePx = 16)
{
    QPixmap pm(sizePx, sizePx);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor base = w ? w->palette().color(QPalette::Text) : QColor(80, 80, 80);

    const QPointF c(sizePx / 2.0, sizePx / 2.0);
    const double outerR = sizePx * 0.45;
    const double innerR = sizePx * 0.22;

    const int N = 12;

    const double step = 360.0 / N;

    p.translate(c);
    p.rotate(angleDeg);

    for (int i = 0; i < N; ++i) {
        double t = double(i) / (N - 1);          // 0..1
        int alpha = int(255 * (1.0 - t) * 0.90); // 0..~230

        QColor col = base;
        col.setAlpha(qBound(20, alpha, 255));

        QPen pen(col, qMax(1.0, sizePx * 0.10), Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);

        p.drawLine(QPointF(0, -innerR), QPointF(0, -outerR));

        p.rotate(step);
    }

    return QIcon(pm);
}

/*!********************************************************************************************************************
 * \brief Enables or disables a loading spinner icon for a tree widget item.
 *
 * When enabled, a timer-driven animated spinner icon is attached to the given
 * QTreeWidgetItem to indicate background processing (e.g. GDS hierarchy loading).
 * When disabled, the spinner is stopped, removed and cleaned up.
 *
 * The animation state is tracked per item to allow multiple independent spinners.
 *
 * \param item Tree widget item to decorate with a spinner.
 * \param on True to start spinner animation, false to stop and remove it.
 *********************************************************************************************************************/
void MainWindow::setLoadingSpinner(QTreeWidgetItem *item, bool on)
{
    if (!item) {
        return;
    }

    QTreeWidget *tw = item->treeWidget();

    if (!on) {
        auto it = m_spinnerStates.find(item);
        if (it != m_spinnerStates.end()) {
            if (it.value().timer) {
                it.value().timer->stop();
                it.value().timer->deleteLater();
                it.value().timer = nullptr;
            }
            m_spinnerStates.erase(it);
        }

        item->setIcon(0, QIcon());
        if (tw) {
            tw->viewport()->update(tw->visualItemRect(item));
        }
        return;
    }

    if (m_spinnerStates.contains(item) && m_spinnerStates[item].timer) {
        return;
    }

    SpinnerState st;
    st.angleDeg = 0;

    auto *timer = new QTimer(this);
    timer->setInterval(60); // скорость (мс). 50-90 обычно норм.

    st.timer = timer;
    m_spinnerStates.insert(item, st);

    item->setIcon(0, makeSpinnerIcon(tw ? (QWidget*)tw : (QWidget*)this, 0, 16));
    if (tw) {
        tw->viewport()->update(tw->visualItemRect(item));
    }

    connect(timer, &QTimer::timeout, this, [this, item]() {
        if (!item) {
            return;
        }

        auto it = m_spinnerStates.find(item);
        if (it == m_spinnerStates.end()) {
            return;
        }

        SpinnerState &st = it.value();
        st.angleDeg = (st.angleDeg + 30) % 360;

        QTreeWidget *tw2 = item->treeWidget();
        item->setIcon(0, makeSpinnerIcon(tw2 ? (QWidget*)tw2 : (QWidget*)this, st.angleDeg, 16));

        if (tw2) {
            tw2->viewport()->update(tw2->visualItemRect(item));
        }
    });

    timer->start();
}

void MainWindow::clearTreeItemChildren(QTreeWidgetItem *parent)
{
    if (!parent) {
        return;
    }
    while (parent->childCount() > 0) {
        delete parent->takeChild(0);
    }
}

void MainWindow::appendProgressCellNames(QTreeWidgetItem *parent,
                                         const QStringList &names,
                                         int pathRole,
                                         const QString &path)
{
    if (!parent || names.isEmpty()) {
        return;
    }

    for (const QString &name : names) {
        auto *cellItem = new QTreeWidgetItem(parent);
        cellItem->setText(0, name);
        cellItem->setData(0, RoleType, ItemCell);
        cellItem->setData(0, RoleCellName, name);
        cellItem->setData(0, pathRole, path);
    }
}

void MainWindow::renameProgressCellNames(QTreeWidgetItem *parent,
                                         const QVector<QPair<QString, QString>> &renames)
{
    if (!parent || renames.isEmpty()) {
        return;
    }

    QHash<QString, QTreeWidgetItem *> byName;
    byName.reserve(parent->childCount());
    for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem *child = parent->child(i);
        byName.insert(child->data(0, RoleCellName).toString(), child);
    }

    for (const auto &pair : renames) {
        QTreeWidgetItem *item = byName.value(pair.first, nullptr);
        if (!item) {
            continue;
        }
        byName.remove(pair.first);
        item->setText(0, pair.second);
        item->setData(0, RoleCellName, pair.second);
        byName.insert(pair.second, item);
    }
}

/*!********************************************************************************************************************
 * \brief Loads GDS hierarchy asynchronously and updates the UI on completion.
 *
 * Starts a background task that scans the GDS file hierarchy using GdsReader and
 * stores the result in the provided cache entry. While loading, a spinner is shown
 * on the target tree item and a status bar message is displayed.
 *
 * On successful completion:
 *  - hierarchy data is cached,
 *  - top-level or requested cell contents are populated in the tree view,
 *  - the target item is expanded automatically.
 *
 * On failure:
 *  - error messages are collected and reported,
 *  - loading state is cleared.
 *
 * The function ensures that the same cache entry is not loaded multiple times
 * concurrently.
 *
 * \param gdsPath Absolute or relative path to the GDS file.
 * \param entry Shared cache entry associated with this GDS file.
 * \param targetItem Tree widget item that represents the GDS node in the UI.
 * \param requestedCellName Optional cell name to expand directly after loading.
 *********************************************************************************************************************/
void MainWindow::loadGdsHierarchyAsync(const QString &gdsPath,
                                       const std::shared_ptr<GdsCacheEntry> &entry,
                                       QTreeWidgetItem *targetItem,
                                       const QString &requestedCellName /* = QString() */)
{
    if (!entry || gdsPath.isEmpty()) {
        return;
    }

    if (entry->loading || entry->loaded) {
        return;
    }

    entry->loading = true;
    statusBar()->showMessage("Scanning GDS hierarchy…", 0);

    if (targetItem) {
        setLoadingSpinner(targetItem, true);
    }

    const bool progressiveRoot = targetItem && requestedCellName.isEmpty();
    QTreeWidgetItem *targetRaw = targetItem;
    QPointer<MainWindow> self(this);
    const QString absPath = QFileInfo(gdsPath).absoluteFilePath();

    auto *watcher = new QFutureWatcher<GdsCacheEntry>(this);

    connect(watcher, &QFutureWatcher<GdsCacheEntry>::finished, this,
            [this, watcher, entry, targetRaw, requestedCellName]()
            {
                const GdsCacheEntry r = watcher->result();
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
                    statusBar()->showMessage("GDS load failed.", 10000);
                    return;
                }

                statusBar()->showMessage(
                    QString("GDS loaded: %1 cells").arg(entry->hierarchy.allCells.size()),
                    10000
                    );

                if (!targetRaw || !targetRaw->treeWidget()) {
                    return;
                }

                clearTreeItemChildren(targetRaw);

                if (!requestedCellName.isEmpty()) {
                    populateCellChildren(targetRaw, entry, requestedCellName);
                    targetRaw->setExpanded(true);
                    return;
                }

                populateGdsTopLevel(targetRaw, entry);
                targetRaw->setExpanded(true);
            });

    auto future = QtConcurrent::run([absPath, self, targetRaw, progressiveRoot]() -> GdsCacheEntry {
        GdsCacheEntry out;
        out.path = absPath;

        QMutex batchMutex;
        QStringList pending;
        QElapsedTimer lastFlush;
        lastFlush.start();
        int reported = 0;

        auto flushProgress = [&](bool force) {
            if (!progressiveRoot || !self || !targetRaw) {
                pending.clear();
                return;
            }

            QStringList batch;
            {
                QMutexLocker lock(&batchMutex);
                if (!force && pending.size() < 48 && lastFlush.elapsed() < 120) {
                    return;
                }
                batch.swap(pending);
                lastFlush.restart();
            }
            if (batch.isEmpty()) {
                return;
            }

            reported += batch.size();
            const int total = reported;
            QMetaObject::invokeMethod(self.data(), [self, targetRaw, absPath, batch, total]() {
                if (!self || !targetRaw || !targetRaw->treeWidget()) {
                    return;
                }
                self->appendProgressCellNames(targetRaw, batch,
                                              MainWindow::RoleGdsPath, absPath);
                self->statusBar()->showMessage(
                    QStringLiteral("Scanning GDS… %1 cells").arg(total), 0);
            }, Qt::QueuedConnection);
        };

        GdsReader reader(out.path);
        if (!reader.readHierarchy(out.hierarchy, [&](const QString &cellName) {
                {
                    QMutexLocker lock(&batchMutex);
                    pending << cellName;
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
