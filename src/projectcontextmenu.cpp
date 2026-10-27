#include <QMenu>
#include <QFile>
#include <QDebug>
#include <QScreen>
#include <QDateTime>
#include <QFileInfo>
#include <QSettings>
#include <QMouseEvent>
#include <QTextStream>
#include <QFileDialog>
#include <QGuiApplication>
#include <QListWidgetItem>
#include <QCheckBox>
#include <QMessageBox>
#include <QSet>
#include <QInputDialog>

#if QT_VERSION >= 0x050000
#include <QScreen>
#endif

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "property.h"

/*!******************************************************************************************************************
 * \brief Deletes folder recursevly.
 * \param dirName     Name of the folder to be deleted.
 *******************************************************************************************************************/
bool MainWindow::removeDir(const QString &dirName) const
{
    bool result = true;
    QDir dir(dirName);

    if (dir.exists(dirName)) {
        Q_FOREACH(QFileInfo info, dir.entryInfoList(QDir::NoDotAndDotDot | QDir::System | QDir::Hidden  |
                                                    QDir::AllDirs | QDir::Files, QDir::DirsFirst)) {
            if (info.isDir()) {
                result = removeDir(info.absoluteFilePath());
            }
            else {
                result = QFile::remove(info.absoluteFilePath());
            }

            if (!result) {
                return result;
            }
        }

        result = dir.rmdir(dirName);
    }

    return result;
}

/*!*****************************************************************************************************************
 * \brief Copies folder recursevly.
 * \param sourceFolder     Name of the source folder.
 * \param destFolder       Name of the target folder.
 ******************************************************************************************************************/
void MainWindow::copyDir(const QString &sourceFolder, const QString &destFolder) const
{
    QDir sourceDir(sourceFolder);
    if(!sourceDir.exists())
        return;

    QDir destDir(destFolder);
    if(!destDir.exists()) {
        destDir.mkdir(destFolder);
    }

    QStringList files = sourceDir.entryList(QDir::Files);
    for(int i = 0; i< files.count(); i++) {
        QString srcName = sourceFolder + "/" + files[i];
        QString destName = destFolder + "/" + files[i];
        QFile::copy(srcName, destName);
    }

    files.clear();
    files = sourceDir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
    for(int i = 0; i< files.count(); i++) {
        QString srcName = sourceFolder + "/" + files[i];
        QString destName = destFolder + "/" + files[i];
        copyDir(srcName, destName);
    }
}

/*!*****************************************************************************************************************
 * \brief Displays a dialog box to confirm user action. For ex., removing folder or file.
 * \param title     Title to be display for user with action description.
 ******************************************************************************************************************/
bool MainWindow::askUserForAction(const QString &title) const
{
    QMessageBox msgBox;
    msgBox.setText(title);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QScreen* pScreen = QGuiApplication::screenAt(this->mapToGlobal(QPoint(this->width() / 2, 0)));
    QRect screenRect = pScreen ? pScreen->availableGeometry() : QRect();
#elif QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QScreen* pScreen = QGuiApplication::screenAt(this->mapToGlobal(QPoint(this->width() / 2, 0)));
    QRect screenRect = pScreen ? pScreen->availableGeometry() : QRect();
#elif QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    QDesktopWidget desktop;
    QRect screenRect = desktop.screenGeometry(this);
#else
    QRect screenRect = QDesktopWidget().screen()->rect();
#endif


    msgBox.move(QPoint(screenRect.width()/2, screenRect.height()/2));

    int ret = msgBox.exec();

    switch (ret) {
        case QMessageBox::Yes:
            return(true);
            break;
        case QMessageBox::No:
            return(false);
            break;
        default:
            break;
    }

    return(false);
}
/*!******************************************************************************************************************
 * \brief Displays a dialog box to confirm file replacement.
 *******************************************************************************************************************/
bool MainWindow::askForFileReplacement() const
{
    return(askUserForAction("File already exists. Would you like to replace it?"));
}

/*!*******************************************************************************************************************
 * \brief Displays delete confirmation with optional permanent removal from disk.
 * \param permanent  Set to true when the user checks "Permanent".
 * \return True if the user confirmed, false on Cancel.
 ********************************************************************************************************************/
bool MainWindow::promptDeleteChoice(bool *permanent) const
{
    QMessageBox msgBox;
    msgBox.setWindowTitle(tr("Delete"));
    msgBox.setText(tr("Remove selected item(s) from the project?"));
    msgBox.setInformativeText(tr("Without Permanent, entries are removed from the project file only."));
    msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Cancel);

    QCheckBox *permanentCheck = new QCheckBox(tr("Permanent (delete from disk)"));
    permanentCheck->setChecked(false);
    msgBox.setCheckBox(permanentCheck);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QScreen *pScreen = QGuiApplication::screenAt(this->mapToGlobal(QPoint(this->width() / 2, 0)));
    QRect screenRect = pScreen ? pScreen->availableGeometry() : QRect();
#elif QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QScreen *pScreen = QGuiApplication::screenAt(this->mapToGlobal(QPoint(this->width() / 2, 0)));
    QRect screenRect = pScreen ? pScreen->availableGeometry() : QRect();
#elif QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    QDesktopWidget desktop;
    QRect screenRect = desktop.screenGeometry(this);
#else
    QRect screenRect = QDesktopWidget().screen()->rect();
#endif

    msgBox.move(QPoint(screenRect.width() / 2, screenRect.height() / 2));

    if(msgBox.exec() != QMessageBox::Ok) {
        return false;
    }

    if(permanent) {
        *permanent = permanentCheck->isChecked();
    }

    return true;
}

/*!*******************************************************************************************************************
 * \brief Displays a dialog box to confirm permanent removing of file.
 ********************************************************************************************************************/
bool MainWindow::askForPermanentDelete() const
{
    bool permanent = false;
    if(!promptDeleteChoice(&permanent)) {
        return false;
    }

    return permanent;
}

/*!*******************************************************************************************************************
 * \brief Creates and displays menu for project group (nested projects).
 ********************************************************************************************************************/
void MainWindow::createProjectUnionMenu()
{
    QMenu *menu = new QMenu(this);

    QAction *proj = new QAction(tr("&Remove"), this);
    proj->setStatusTip(tr("Remove group"));
    connect(proj, SIGNAL(triggered()), this, SLOT(removeGroupUnion()));
    menu->addAction(proj);

    QMenu *menuGroup = menu->addMenu("Add To Group");

    QString curProjName = getCurrentUnionName();
    if(curProjName.isEmpty()) {
        return;
    }

    for(int i = 0; i < m_ui->treeLibs->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_ui->treeLibs->topLevelItem(i);
        if(!item) {
            continue;
        }

        if(item->childCount()) {
            continue;
        }

        QString projName = item->text(0);
        if(projName.isEmpty()) {
            continue;
        }

        if(projName == curProjName) {
            continue;
        }

        QAction *projId = new QAction(projName, this);
        projId->setStatusTip(projName);
        connect(projId, SIGNAL(triggered()), this, SLOT(mergeProjectIntoGroup()));
        menuGroup->addAction(projId);
    }

    menu->addMenu(menuGroup);

    menu->popup(QCursor::pos());
    menu->exec();

    delete menu;
}

/*!******************************************************************************************************************
 * \brief Creates and displays menu for project (library) tree widget. It is used for projects management.
 * \param pos       Point(x, y) where to display menu.
 *******************************************************************************************************************/
void MainWindow::showLibraryMenu(const QPoint &pos)
{
    QTreeWidgetItem *currentItem = m_ui->treeLibs->currentItem();
    if(currentItem && currentItem->data(0, RoleType).toInt() == ItemPdkRoot) {
        QMenu *menu = new QMenu(this);
        QAction *infoAct = new QAction(tr("&Info"), this);
        connect(infoAct, &QAction::triggered, this, [this, currentItem]() {
            const QString path = currentItem->data(0, RolePdkPath).toString();
            const QString pdkName = currentItem->text(0);
            QString extra = QStringLiteral("\tType: Technology PDK\n");
            for (const DoorPdkInfo &pdk : m_loadedPdks) {
                if (pdk.name != pdkName) {
                    continue;
                }
                extra += QStringLiteral("\tDescriptor: ")
                    + (pdk.descriptorPath.isEmpty() ? QStringLiteral("(auto-detect)")
                                                    : pdk.descriptorPath)
                    + QLatin1Char('\n');
                extra += QStringLiteral("\tlibs.tech layers: ") + pdk.tech.layers + QLatin1Char('\n');
                extra += QStringLiteral("\tlibs.tech DRC: ") + pdk.tech.drc + QLatin1Char('\n');
                extra += QStringLiteral("\tlibs.tech LVS: ") + pdk.tech.lvs + QLatin1Char('\n');
                break;
            }
            showFolderInfo(QStringLiteral("PDK"), pdkName,
                           path.isEmpty() ? getLibraryPath(pdkName) : path,
                           true, extra);
        });
        menu->addAction(infoAct);

        QAction *descAct = new QAction(tr("Edit &Descriptor…"), this);
        descAct->setStatusTip(tr("Open .door-pdk.json (JSON editor + resolved path tree)."));
        connect(descAct, &QAction::triggered, this, [this, currentItem]() {
            const QString pdkName = currentItem->text(0);
            const QString path = currentItem->data(0, RolePdkPath).toString();
            editPdkDescriptor(pdkName, path);
        });
        menu->addAction(descAct);

        QAction *removeAct = new QAction(tr("&Remove PDK"), this);
        connect(removeAct, &QAction::triggered, this, [this, currentItem]() {
            const QString pdkName = currentItem->text(0);
            for (int i = 0; i < m_loadedPdks.size(); ++i) {
                if (m_loadedPdks.at(i).name != pdkName) {
                    continue;
                }
                for (const DoorPdkLibrary &lib : m_loadedPdks.at(i).libraries) {
                    removeLibraryPropertyKeys(lib.name);
                }
                m_loadedPdks.removeAt(i);
                break;
            }
            loadLibraries();
            applyPdkTreeGrouping();
            setStateChanged();
            if (!m_currentProjFile.isEmpty()) {
                saveProjectFile(m_currentProjFile);
            }
        });
        menu->addAction(removeAct);
        menu->popup(m_ui->treeLibs->mapToGlobal(pos));
        return;
    }
    if(currentItem && currentItem->childCount()) {
        createProjectUnionMenu();
        return;
    }

    QMouseEvent event(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mousePressEvent(&event);

    QMenu *menu = new QMenu(this);

    QAction *proj = new QAction(tr("&Add New..."), this);
    proj->setIcon(QIcon(":/icons/category.svg"));
    proj->setStatusTip(tr("Add a library directory, or a PDK if .door-pdk.json / Open-PDK layout is present."));
    connect(proj, &QAction::triggered, this, &MainWindow::addNewProject);
    menu->addAction(proj);

    QList<QTreeWidgetItem *> items = m_ui->treeLibs->selectedItems();
    if(items.count()) {
        menu->addSeparator();

        QAction *copyProj = new QAction(tr("&Copy"), this);
        copyProj->setIcon(QIcon(":/icons/copy.svg"));
        copyProj->setShortcut(QKeySequence::Copy);
        copyProj->setStatusTip(tr("Copy project."));
        copyProj->setShortcutContext(Qt::WidgetShortcut);
        connect(copyProj, &QAction::triggered, this, &MainWindow::copySelectedProject);
        menu->addAction(copyProj);
        addAction(copyProj);

        if(isProjectCopied()) {
            QAction *pasteProj = new QAction(tr("&Paste"), this);
            pasteProj->setIcon(QIcon(":/icons/paste.svg"));
            pasteProj->setShortcut(QKeySequence::Paste);
            pasteProj->setStatusTip(tr("Paste project."));
            pasteProj->setShortcutContext(Qt::WidgetShortcut);
            connect(pasteProj, &QAction::triggered, this, &MainWindow::pasteSelectedData);
            menu->addAction(pasteProj);
            addAction(pasteProj);
        }

        QAction *delProj = new QAction(tr("&Delete"), this);
        delProj->setIcon(QIcon(":/icons/delete.svg"));
        delProj->setShortcut(QKeySequence::Delete);
        delProj->setStatusTip(tr("Delete project."));
        delProj->setShortcutContext(Qt::WidgetShortcut);
        connect(delProj, &QAction::triggered, this, &MainWindow::removeSelectedProject);
        menu->addAction(delProj);
        addAction(delProj);

        QAction *projInfo = new QAction(tr("&Info"), this);
        projInfo->setIcon(QIcon(":/icons/info.svg"));
        projInfo->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));
        projInfo->setStatusTip(tr("Show project information."));
        projInfo->setShortcutContext(Qt::WidgetShortcut);
        connect(projInfo, &QAction::triggered, this, &MainWindow::showProjectInfo);
        menu->addAction(projInfo);
        addAction(projInfo);

        QAction *renameLib = new QAction(tr("&Rename"), this);
        renameLib->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
        renameLib->setShortcut(QKeySequence(Qt::Key_F2));
        renameLib->setStatusTip(tr("Rename selected library."));
        renameLib->setShortcutContext(Qt::WidgetShortcut);
        connect(renameLib, &QAction::triggered, this, &MainWindow::renameSelectedLibrary);
        menu->addAction(renameLib);
        addAction(renameLib);

        QAction *addExisting = new QAction(tr("Add &Existing Cell..."), this);
        addExisting->setIcon(QIcon(":/icons/view.svg"));
        addExisting->setStatusTip(tr("Add existing supported cell view."));
        connect(addExisting, &QAction::triggered, this, &MainWindow::addExistingCell);
        menu->addAction(addExisting);

        menu->addSeparator();

        const QStringList attachedTechs = getTechLibraryAttachList(getCurrentLibraryName());
        if(!attachedTechs.isEmpty()) {
            QAction *techInfo = new QAction(
                tr("Tech libraries: %1").arg(attachedTechs.join(QStringLiteral(", "))), this);
            techInfo->setEnabled(false);
            menu->addAction(techInfo);
        }

        QAction *attachTech = new QAction(tr("Attach Tech Library..."), this);
        attachTech->setStatusTip(tr("Add a tech/primitive ROOM library for schematic tools (PDK, analogLib, commonLib, …)."));
        connect(attachTech, &QAction::triggered, this, &MainWindow::attachTechLibrary);
        menu->addAction(attachTech);

        if(!attachedTechs.isEmpty()) {
            QAction *detachTech = new QAction(tr("Detach Tech Library..."), this);
            connect(detachTech, &QAction::triggered, this, &MainWindow::detachTechLibrary);
            menu->addAction(detachTech);
        }

        QMap<QString, QString> projects = getCurrentLibraries();
        if(projects.count() && currentItem && !currentItem->parent()) {
            QMenu *menuGroup = menu->addMenu(tr("Group with"));
            menuGroup->setIcon(QIcon(":/icons/category.svg"));

            const QString curProjName = getCurrentLibraryName();
            if(curProjName.isEmpty()) {
                delete menu;
                return;
            }

            QMap<QString, QString>::const_iterator it;
            for(it = projects.constBegin(); it != projects.constEnd(); ++it) {
                const QString projName = it.key();

                if(projName.isEmpty()) {
                    continue;
                }

                if(projName == curProjName) {
                    continue;
                }

                QTreeWidgetItem *item = getTreeItemByName(projName);
                if(item && item->parent()) {
                    continue;
                }

                QAction *projId = new QAction(projName, this);
                projId->setIcon(QIcon(":/icons/category.svg"));
                projId->setStatusTip(projName);
                connect(projId, &QAction::triggered, this, &MainWindow::mergeProjectIntoGroup);
                menuGroup->addAction(projId);
            }
        }
        else if(currentItem && currentItem->parent()) {
            QAction *ungroupInfo = new QAction(tr("&Ungroup"), this);
            ungroupInfo->setIcon(QIcon(":/icons/delete.svg"));
            ungroupInfo->setStatusTip(tr("Remove project from group."));
            connect(ungroupInfo, &QAction::triggered, this, &MainWindow::removeFromGroup);
            menu->addAction(ungroupInfo);
        }
    }

    menu->addSeparator();

    QMenu *gitMenu = menu->addMenu(tr("Git"));
    gitMenu->setIcon(QIcon(":/icons/git_branch.svg"));

    QAction *gitStatus = new QAction(tr("Status"), this);
    gitStatus->setIcon(QIcon(":/icons/git_status.svg"));
    gitStatus->setStatusTip(tr("Show git status."));
    connect(gitStatus, &QAction::triggered, this, &MainWindow::gitShowStatus);
    gitMenu->addAction(gitStatus);

    QAction *gitCommit = new QAction(tr("Commit"), this);
    gitCommit->setIcon(QIcon(":/icons/git_commit.svg"));
    gitCommit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    gitCommit->setStatusTip(tr("Commit changes."));
    gitCommit->setShortcutContext(Qt::WidgetShortcut);
    connect(gitCommit, &QAction::triggered, this, &MainWindow::gitCommitChanges);
    gitMenu->addAction(gitCommit);
    addAction(gitCommit);

    QAction *gitLog = new QAction(tr("Log"), this);
    gitLog->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    gitLog->setStatusTip(tr("Show git log."));
    connect(gitLog, &QAction::triggered, this, &MainWindow::gitShowLog);
    gitMenu->addAction(gitLog);

    QAction *gitDiff = new QAction(tr("Diff"), this);
    gitDiff->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    gitDiff->setStatusTip(tr("Show git diff."));
    connect(gitDiff, &QAction::triggered, this, &MainWindow::gitShowDiff);
    gitMenu->addAction(gitDiff);

    QAction *gitPull = new QAction(tr("Pull"), this);
    gitPull->setIcon(QIcon(":/icons/git_pull.svg"));
    gitPull->setStatusTip(tr("Pull changes from remote repository."));
    connect(gitPull, &QAction::triggered, this, &MainWindow::gitPull);
    gitMenu->addAction(gitPull);

    QAction *gitPush = new QAction(tr("Push"), this);
    gitPush->setIcon(QIcon(":/icons/git_push.svg"));
    gitPush->setStatusTip(tr("Push changes to remote repository."));
    connect(gitPush, &QAction::triggered, this, &MainWindow::gitPush);
    gitMenu->addAction(gitPush);

    QAction *gitCheckout = new QAction(tr("Checkout..."), this);
    gitCheckout->setIcon(QIcon(":/icons/git_branch.svg"));
    gitCheckout->setStatusTip(tr("Checkout branch."));
    connect(gitCheckout, &QAction::triggered, this, &MainWindow::gitCheckout);
    gitMenu->addAction(gitCheckout);

    menu->exec(QCursor::pos());

    delete menu;
}

/*!******************************************************************************************************************
 * \brief Copies selected project to the buffer.
 *******************************************************************************************************************/
void MainWindow::copySelectedProject()
{
    QList<QTreeWidgetItem *> items = m_ui->treeLibs->selectedItems();
    if(!items.count()) {
        return;
    }

    clearCurrentCopyState();

    foreach(QTreeWidgetItem *item, items) {
        if(!item->text(0).isEmpty()) {
            addProjectToBeCopied(item->text(0));
        }
    }
}

/*!******************************************************************************************************************
 * \brief Pastes selected data (could be project, group or file.
 *******************************************************************************************************************/
void MainWindow::pasteSelectedData()
{
    if(!m_copyData.count()) {
        return;
    }

    if(m_currentCopyState == NONE) {
        return;
    }

    if(isProjectCopied()) {
        foreach(const QString &projName, m_copyData) {
            QString key = getLibraryKeyPrefix() + projName;
            QString projPath = m_properties->get<QString>(key);

            if(QFileInfo(projPath).isDir()) {
                QString targetName = generateCopyName(projName, QFileInfo(projPath).absolutePath());

                QString libName = QFileInfo(targetName).completeBaseName();
                info(QString("Coping '%1'' to '%2'...").arg(projName).arg(libName), false);

                copyDir(projPath, targetName);

                QTreeWidgetItem *item = new QTreeWidgetItem;
                item->setText(0, libName);
                item->setFlags(item->flags() | Qt::ItemIsEditable);
                m_ui->treeLibs->addTopLevelItem(item);

                key = getLibraryKeyPrefix() + libName;
                m_properties->set(key, targetName);

                setStateChanged();
            }
        }
    }
    else if(isGroupCopied()) {
        if(m_copyData.count() != 2) {
            return;
        }

        QString groupName = m_copyData[0];
        QString groupPath = m_copyData[1];

        if(!QFileInfo(groupPath).isDir()) {
            return;
        }

        QStringList viewsToBeCopied;

        QStringList views = getValidViewList();
        foreach(const QString &viewName, views) {
            QString viewPath = QDir::toNativeSeparators(groupPath + "/" + viewName + "/" + groupName + "." + viewName);
            if(QFileInfo(viewPath).exists()) {
                viewsToBeCopied<<viewPath;
            }
        }

        QString tarLibPath = getCurrentLibraryPath();
        if(!QFileInfo(tarLibPath).isDir()) {
            return;
        }

        QMap<QString, QString> copyMap;

        bool askForReplacement = false;
        foreach(const QString &viewPath, viewsToBeCopied) {
            QString tarViewName = QFileInfo(viewPath).completeSuffix();
            QString tarGroupName = QFileInfo(viewPath).completeBaseName();
            QString tarViewPath = QDir::toNativeSeparators(tarLibPath + "/" + tarViewName + "/" + tarGroupName + "." + tarViewName);
            copyMap[viewPath] = tarViewPath;

            if(QFileInfo(tarViewPath).exists()) {
                askForReplacement = true;
            }
        }

        if(askForReplacement) {
            if(!askForFileReplacement()) {
                return;
            }
        }

        QMap<QString, QString>::const_iterator it;
        for(it = copyMap.constBegin(); it != copyMap.constEnd(); ++it) {
            QString src = it.key();
            QString tar = it.value();

            if(!QFileInfo(src).exists()) {
                continue;
            }

            QString tarPath = QFileInfo(tar).absolutePath();
            if(!QFileInfo(tarPath).isDir()) {
                QDir dir;
                dir.mkpath(tarPath);
            }

            info(QString("Coping view '%1' to '%2'...").arg(src).arg(tar));

            QFile::copy(src, tar);

            if(QFileInfo(tar).exists()) {
                QString viewName = QFileInfo(tar).completeSuffix();
                QString groupName = QFileInfo(tar).completeBaseName();

                if(viewName.isEmpty() || groupName.isEmpty()) {
                    continue;
                }

                QListWidgetItem *itemGroup = new QListWidgetItem;
                itemGroup->setText(groupName);
                itemGroup->setFlags(itemGroup->flags() | Qt::ItemIsEditable);
                m_ui->listGroups->addItem(itemGroup);
                m_ui->listGroups->setCurrentItem(itemGroup);

                QTreeWidgetItem *itemView = new QTreeWidgetItem(m_ui->listViews);
                itemView->setText(0, viewName);

                if(viewName == "gds") {
                    itemView->setData(0, RoleType, ItemViewGds);
                    itemView->setData(0, RoleGdsPath, tar);
                    itemView->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
                }

                setStateChanged();
            }
        }
    }
    else if(isViewCopied()) {
        foreach(const QString &viewPath, m_copyData) {
            if(QFileInfo(viewPath).isFile()) {
                QString viewName = QFileInfo(viewPath).completeSuffix();
                if(viewName.isEmpty()) {
                    continue;
                }

                QString groupName = getCurrentGroupName();
                if(groupName.isEmpty()) {
                    continue;
                }

                QString groupPath = getCurrentGroupPath(viewName, true);
                if(!QFileInfo(groupPath).isDir()) {
                    continue;
                }

                QString newViewPath = QDir::toNativeSeparators(groupPath + "/" + groupName + "." + viewName);
                if(QFileInfo(newViewPath).exists()) {
                    if(!askForFileReplacement()) {
                        continue;
                    }
                    else {
                        QFile::remove(newViewPath);
                    }
                }

                QFile::copy(viewPath, newViewPath);
                if(QFileInfo(newViewPath).exists()) {
                    info(QString("Coping view '%1' to '%2'").arg(viewPath).arg(newViewPath));
                }
                else {
                    error(QString("Failed to copy view '%1' to '%2'").arg(viewPath).arg(newViewPath));
                    continue;
                }

                QTreeWidgetItem *item = new QTreeWidgetItem(m_ui->listViews);
                item->setText(0, viewName);

                if(viewName == "gds") {
                    item->setData(0, RoleType, ItemViewGds);
                    item->setData(0, RoleGdsPath, newViewPath);
                    item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
                }

                setStateChanged();
            }
        }
    }

    m_copyData.clear();
    m_currentCopyState = NONE;
}

/*!*****************************************************************************************************************
 * \brief Creates new project (library) and adds it to the tree widget.
 ******************************************************************************************************************/
void MainWindow::addNewProject()
{
    QString workDir = getCurrentWorkingDir();
    QString projName = QFileDialog::getExistingDirectory(this,
                                                         tr("Choose or create a new project directory."),
                                                         workDir);

    if (projName.isEmpty()) {
        return;
    }

    // PDK if .door-pdk.json exists (or classic Open-PDK libs.ref + libs.tech layout).
    if (tryAddDirectoryAsPdk(projName)) {
#if QT_VERSION >= 0x050000
        m_ui->treeLibs->sortByColumn(0, Qt::AscendingOrder);
#else
        m_ui->treeLibs->sortByColumn(0);
#endif
        return;
    }

    QString libName = QFileInfo(projName).completeBaseName();

    bool addProj = true;
    for (int i = 0; i < m_ui->treeLibs->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_ui->treeLibs->topLevelItem(i);
        if (item && item->text(0) == libName) {
            addProj = false;
            break;
        }
    }

    if (addProj) {
        QTreeWidgetItem *item = new QTreeWidgetItem;
        item->setText(0, libName);
        m_ui->treeLibs->addTopLevelItem(item);

        setLibraryRootDirectory(libName, projName);

        m_ui->treeLibs->setCurrentItem(item);
        on_treeLibs_itemClicked(item, 0);

        if (!m_currentProjFile.isEmpty()) {
            saveProjectFile(m_currentProjFile);
        } else {
            setStateChanged();
        }
    }

#if QT_VERSION >= 0x050000
    m_ui->treeLibs->sortByColumn(0, Qt::AscendingOrder);
#else
    m_ui->treeLibs->sortByColumn(0);
#endif
}

/*!******************************************************************************************************************
 * \brief Removes selected project (library).
 *******************************************************************************************************************/
void MainWindow::removeSelectedProject()
{
    QList<QTreeWidgetItem *> items = m_ui->treeLibs->selectedItems();
    if(!items.count()) {
        return;
    }

    bool deleteProject = false;
    if(!promptDeleteChoice(&deleteProject)) {
        return;
    }

    QSet<QString> libNames;
    for(QTreeWidgetItem *item : items) {
        if(!item || item->childCount()) {
            continue;
        }

        const QString libName = item->text(0).trimmed();
        if(!libName.isEmpty()) {
            libNames.insert(libName);
        }
    }

    if(libNames.isEmpty()) {
        return;
    }

    bool changed = false;
    for(const QString &libName : libNames) {
        const QString libPath = getLibraryPath(libName);

        for(int j = 0; j < m_ui->treeLibs->topLevelItemCount(); ++j) {
            QTreeWidgetItem *item = m_ui->treeLibs->topLevelItem(j);
            if(!item) {
                continue;
            }

            if(item->text(0) == libName && !item->childCount()) {
                delete m_ui->treeLibs->takeTopLevelItem(j);
                break;
            }

            if(item->childCount()) {
                for(int k = 0; k < item->childCount(); ++k) {
                    QTreeWidgetItem *child = item->child(k);
                    if(child && child->text(0) == libName) {
                        delete item->takeChild(k);
                        break;
                    }
                }
            }
        }

        removeLibraryPropertyKeys(libName);

        if(deleteProject && !libPath.isEmpty()) {
            info(QString("Removing library '%1'").arg(libPath), false);
            removeDir(libPath);
        }

        changed = true;
    }

    if(changed) {
        setStateChanged();
        if(!m_currentProjFile.isEmpty()) {
            saveProjectFile(m_currentProjFile);
        }
    }

#if QT_VERSION >= 0x050000
    m_ui->treeLibs->sortByColumn(0, Qt::AscendingOrder);
#else
    m_ui->treeLibs->sortByColumn(0);
#endif
}

/*!*********************************************************************************************************************
 * \brief Prints folder Unix information into the MainWindow output window.
 * \param title       Title to print (for ex., Library, Cell, Category, View).
 * \param alias       If file is aliased then it prints the real (aliased) name.
 * \param folderPath  Path to the folder which is requested for printing its information.
 * \param clear       Clears MainWindow output window before printing message.
 **********************************************************************************************************************/
void MainWindow::showFolderInfo(const QString &title, const QString &alias, const QString &folderPath, bool clear, const QString &extraLines)
{
    if(!QFileInfo(folderPath).exists()) {
        return;
    }

    QString owner = QFileInfo(folderPath).owner();
    QString group = QFileInfo(folderPath).group();
    QString lastModify = QFileInfo(folderPath).lastModified().toString("dd:mm:yyyy");

    QString permissions = "";
    if(QFileInfo(folderPath).isReadable()) {
        permissions += "r";
    }
    else {
        permissions += "-";
    }

    if(QFileInfo(folderPath).isWritable()) {
        permissions += "w";
    }
    else {
        permissions += "-";
    }

    if(QFileInfo(folderPath).isExecutable()) {
        permissions += "x";
    }
    else {
        permissions += "-";
    }

    QString msg = title + ": \n";
    msg += "\tName: " + alias + "\n";
    msg += "\tRead Path: " + folderPath + "\n";
    msg += "\tOwner: " + owner + "\n";
    msg += "\tGroup: " + group + "\n";
    msg += "\tLast Modify: " + lastModify + "\n";
    msg += "\tPermissions: " + permissions + "\n";
    if(!extraLines.isEmpty()) {
        msg += extraLines;
    }

    info(msg, clear);
}

/*!*********************************************************************************************************************
 * \brief Prints library folder Unix information into the MainWindow output window.
 **********************************************************************************************************************/
void MainWindow::showProjectInfo()
{
    QList<QTreeWidgetItem *> items = m_ui->treeLibs->selectedItems();
    if(!items.count()) {
        return;
    }

    QTreeWidgetItem *projId = items.first();
    QString projName = projId->text(0);
    if(projName.isEmpty()) {
        return;
    }

    QString libPath = getLibraryPath(projName);
    if(!QFileInfo(libPath).isDir()) {
        return;
    }

    QString extra;
    const QStringList techLibs = getTechLibraryAttachList(projName);
    if(!techLibs.isEmpty()) {
        extra += QStringLiteral("\tTech library: ") + techLibs.join(QStringLiteral(", ")) + QLatin1Char('\n');
        for(const QString &techLib : techLibs) {
            const QString corePath = resolveTechLibraryCorePath(techLib);
            if(!corePath.isEmpty()) {
                extra += QStringLiteral("\tTech ROOM (%1): ").arg(techLib) + corePath + QLatin1Char('\n');
            }
            else {
                extra += QStringLiteral("\tTech ROOM (%1): (not found)\n").arg(techLib);
            }
        }
    }
    else {
        const QStringList usedBy = getDesignLibrariesUsingTech(projName);
        if(!usedBy.isEmpty()) {
            extra += QStringLiteral("\tRole: tech library (primitives)\n");
            extra += QStringLiteral("\tAttached to: ") + usedBy.join(QStringLiteral(", ")) + QLatin1Char('\n');
            const QString corePath = resolveTechLibraryCorePath(projName);
            if(!corePath.isEmpty()) {
                extra += QStringLiteral("\tSymbol cores: ") + corePath + QLatin1Char('\n');
            }
        }
        else {
            extra += QStringLiteral("\tTech library: (none)\n");
        }
    }

    showFolderInfo(QStringLiteral("Project"), projName, libPath, true, extra);
}

/*!******************************************************************************************************************
 * \brief Clears current buffer used for coping of data.
 *******************************************************************************************************************/
void MainWindow::clearCurrentCopyState()
{
    m_copyData.clear();
    m_currentCopyState = NONE;
}

/*!******************************************************************************************************************
 * \brief Adds project to the buffer for later coping.
 * \param projName     Name of the project (library) to be copied.
 *******************************************************************************************************************/
void MainWindow::addProjectToBeCopied(const QString &projName)
{
    m_copyData<<projName;
    m_currentCopyState = PROJECT;
}

/*!******************************************************************************************************************
 * \brief Merges project into a nested group.
 *******************************************************************************************************************/
void MainWindow::mergeProjectIntoGroup()
{
    QObject *object = QObject::sender();
    if(!object) {
        return;
    }

    QAction *action = static_cast<QAction*> (object);
    if(!action) {
        return;
    }

    QString targetProj = action->text();
    if(targetProj.isEmpty()) {
        return;
    }

    QTreeWidgetItem *targetId = getTreeItemByName(targetProj);
    if(!targetId) {
        return;
    }

    QTreeWidgetItem *sourceId = getTreeItemByName(!m_itemText.isEmpty() ? m_itemText : getCurrentUnionName());
    if(!sourceId) {
        return;
    }

    if(!sourceId->childCount() && targetId->childCount()) {
        std::swap(sourceId, targetId);
    }

    if(!sourceId->childCount()) {
        QTreeWidgetItem *item = new QTreeWidgetItem;
        item->setText(0, "GroupName");
        item->setFlags(item->flags() | Qt::ItemIsEditable);

        m_ui->treeLibs->takeTopLevelItem(m_ui->treeLibs->indexOfTopLevelItem(sourceId));
        m_ui->treeLibs->takeTopLevelItem(m_ui->treeLibs->indexOfTopLevelItem(targetId));

        QTreeWidgetItem *proj1 = new QTreeWidgetItem;
        proj1->setText(0, m_itemText);
        proj1->setFlags(proj1->flags() | Qt::ItemIsEditable);
        item->addChild(proj1);

        QTreeWidgetItem *proj2 = new QTreeWidgetItem;
        proj2->setText(0, targetProj);
        proj2->setFlags(proj2->flags() | Qt::ItemIsEditable);
        item->addChild(proj2);

        m_ui->treeLibs->addTopLevelItem(item);

        setStateChanged();
    }
    else {
        QTreeWidgetItem *proj2 = new QTreeWidgetItem;
        proj2->setText(0, targetProj);
        proj2->setFlags(proj2->flags() | Qt::ItemIsEditable);

        m_ui->treeLibs->takeTopLevelItem(m_ui->treeLibs->indexOfTopLevelItem(targetId));

        sourceId->addChild(proj2);

        setStateChanged();
    }

#if QT_VERSION >= 0x050000
    m_ui->treeLibs->sortByColumn(0, Qt::AscendingOrder);
#else
    m_ui->treeLibs->sortByColumn(0);
#endif
}

/*!******************************************************************************************************************
 * \brief Eliminates project from the nested group.
 *******************************************************************************************************************/
void MainWindow::removeFromGroup()
{
    QString projName = getCurrentLibraryName();
    if(projName.isEmpty()) {
        return;
    }

    QTreeWidgetItem *projId = getTreeItemByName(projName);
    if(!projId) {
        return;
    }

    QTreeWidgetItem *parentId = projId->parent();
    if(parentId) {
        QTreeWidgetItem *newProjId = new QTreeWidgetItem;
        newProjId->setText(0, projName);
        newProjId->setFlags(newProjId->flags() | Qt::ItemIsEditable);
        m_ui->treeLibs->addTopLevelItem(newProjId);

        parentId->takeChild(parentId->indexOfChild(projId));

        setStateChanged();
    }

#if QT_VERSION >= 0x050000
    m_ui->treeLibs->sortByColumn(0, Qt::AscendingOrder);
#else
    m_ui->treeLibs->sortByColumn(0);
#endif
}

/*!******************************************************************************************************************
 * \brief Delets group (nested projects) from the tree widget.
 *******************************************************************************************************************/
void MainWindow::removeGroupUnion()
{
    QString groupName = getCurrentUnionName();
    if(groupName.isEmpty()) {
        return;
    }

    for(int i = 0; i < m_ui->treeLibs->topLevelItemCount(); ++i) {
        QTreeWidgetItem *groupId = m_ui->treeLibs->topLevelItem(i);
        if(!groupId) {
            continue;
        }

        QString curItemName = groupId->text(0);
        if(curItemName == groupName) {
            while(groupId->childCount()) {
                int childIndex = groupId->childCount()-1;
                QTreeWidgetItem *childId = groupId->child(childIndex);
                if(!childId) {
                    continue;
                }

                QString projName = childId->text(0);
                if(projName.isEmpty()) {
                    continue;
                }

                QTreeWidgetItem *newProjId = new QTreeWidgetItem;
                newProjId->setText(0, projName);
                newProjId->setFlags(newProjId->flags() | Qt::ItemIsEditable);
                m_ui->treeLibs->addTopLevelItem(newProjId);

                groupId->takeChild(childIndex);
            }

            m_ui->treeLibs->takeTopLevelItem(i);

            setStateChanged();
        }
    }

#if QT_VERSION >= 0x050000
    m_ui->treeLibs->sortByColumn(0, Qt::AscendingOrder);
#else
    m_ui->treeLibs->sortByColumn(0);
#endif
}

/*!******************************************************************************************************************
 * \brief Attaches a tech/primitive library from this project to the selected design library.
 *******************************************************************************************************************/
void MainWindow::attachTechLibrary()
{
    const QString designLib = getCurrentLibraryName();
    if(designLib.isEmpty()) {
        error(tr("Select a project library first."));
        return;
    }

    const QStringList already = getTechLibraryAttachList(designLib);
    QStringList candidates = registeredLibraryNames();
    candidates.removeAll(designLib);
    for(const QString &tech : already) {
        candidates.removeAll(tech);
    }
    candidates.sort(Qt::CaseInsensitive);

    if(candidates.isEmpty()) {
        if(!already.isEmpty()) {
            QMessageBox::information(this,
                                     tr("Attach Tech Library"),
                                     tr("All other libraries are already attached to '%1':\n%2")
                                         .arg(designLib, already.join(QStringLiteral(", "))));
        }
        else {
            QMessageBox::information(this,
                                     tr("Attach Tech Library"),
                                     tr("No other libraries in this project.\n"
                                        "Add a tech library (e.g. sg13g2_pr) via define() first."));
        }
        return;
    }

    bool ok = false;
    const QString label = already.isEmpty()
        ? tr("Tech library for '%1':").arg(designLib)
        : tr("Add tech library for '%1'\n(already: %2):")
              .arg(designLib, already.join(QStringLiteral(", ")));
    const QString techLib = QInputDialog::getItem(this,
                                                  tr("Attach Tech Library"),
                                                  label,
                                                  candidates,
                                                  0,
                                                  false,
                                                  &ok);
    if(!ok || techLib.trimmed().isEmpty()) {
        return;
    }

    addTechLibraryAttach(designLib, techLib.trimmed());
    setStateChanged();

    if(!m_currentProjFile.isEmpty()) {
        saveProjectFile(m_currentProjFile);
    }

    info(tr("Attached tech library '%1' to '%2'.").arg(techLib.trimmed(), designLib));
}

/*!******************************************************************************************************************
 * \brief Removes one tech library attachment from the selected design library.
 *******************************************************************************************************************/
void MainWindow::detachTechLibrary()
{
    const QString designLib = getCurrentLibraryName();
    if(designLib.isEmpty()) {
        return;
    }

    QStringList techs = getTechLibraryAttachList(designLib);
    if(techs.isEmpty()) {
        return;
    }

    QString toRemove = techs.first();
    if(techs.size() > 1) {
        bool ok = false;
        toRemove = QInputDialog::getItem(this,
                                         tr("Detach Tech Library"),
                                         tr("Detach from '%1':").arg(designLib),
                                         techs,
                                         0,
                                         false,
                                         &ok);
        if(!ok || toRemove.trimmed().isEmpty()) {
            return;
        }
        toRemove = toRemove.trimmed();
    }

    techs.removeAll(toRemove);
    if(techs.isEmpty()) {
        clearTechLibraryAttach(designLib);
    }
    else {
        m_properties->set(techAttachPropertyKey(designLib), techs.join(QLatin1Char(';')));
    }
    setStateChanged();

    if(!m_currentProjFile.isEmpty()) {
        saveProjectFile(m_currentProjFile);
    }

    info(tr("Detached tech library '%1' from '%2'.").arg(toRemove, designLib));
}
