#include <QMenu>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QProcess>
#include <QProcessEnvironment>
#include <QVariant>
#include <QFileInfo>
#include <QSettings>
#include <QMouseEvent>
#include <QTextStream>
#include <QFileDialog>
#include <QListWidgetItem>
#include <QRegularExpression>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QSet>
#include <QDirIterator>
#include <QDateTime>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "property.h"
#include "libfileparser.h"
#include "libdefine_utils.h"
#include "room/room_path_utils.h"
#include "libman_test_mode.h"

/*!******************************************************************************************************************
 * \brief Detects view name from file suffix.
 *******************************************************************************************************************/
QString MainWindow::detectViewFromPath(const QString& filePath) const
{
    QString groupName;
    QString viewName;
    if (resolveCellViewFromPath(filePath, &groupName, &viewName)) {
        return viewName;
    }
    return QString();
}

bool MainWindow::resolveCellViewFromPath(const QString &filePath,
                                         QString *groupName,
                                         QString *viewName) const
{
    if (!groupName || !viewName) {
        return false;
    }

    const RoomViewIdentity emIdentity = parseEmSetupIdentity(filePath);
    if (emIdentity.valid) {
        *groupName = emIdentity.cellName;
        *viewName = emIdentity.viewName;
        return true;
    }

    const RoomViewIdentity coreIdentity = parseRoomViewIdentity(filePath);
    if (coreIdentity.valid) {
        *groupName = coreIdentity.cellName;
        *viewName = coreIdentity.viewName;
        return true;
    }

    const QFileInfo fi(filePath);
    const QString suffix = fi.suffix().trimmed().toLower();
    if (suffix.isEmpty()) {
        return false;
    }

    const QString cellName = fi.completeBaseName().trimmed();
    if (cellName.isEmpty()) {
        return false;
    }

    *groupName = cellName;
    *viewName = suffix;
    return true;
}

void MainWindow::configureCoreViewTreeItem(QTreeWidgetItem *viewItem,
                                           const QString &viewName,
                                           const QString &viewPath) const
{
    if (!viewItem || !isRoomViewName(viewName)) {
        return;
    }

    viewItem->setData(0, RoleType, ItemViewCore);
    viewItem->setData(0, RoleCorePath, viewPath);
    applyViewTreeIcon(viewItem, viewName);
    if (isLayoutRoomViewName(viewName)) {
        viewItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }
}

void MainWindow::configureEmSetupViewTreeItem(QTreeWidgetItem *viewItem,
                                              const QString &viewPath) const
{
    if (!viewItem) {
        return;
    }
    viewItem->setData(0, RoleType, ItemViewEmSetup);
    viewItem->setData(0, RoleEmSetupPath, viewPath);
    applyViewTreeIcon(viewItem, QStringLiteral("emsetup"));
    viewItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
}

void MainWindow::applyViewTreeIcon(QTreeWidgetItem *viewItem, const QString &viewName) const
{
    if (!viewItem) {
        return;
    }
    const QString view = viewName.trimmed().toLower();
    // emsetup: icon only in Views → New menu, not in the View tree.
    if (view == QStringLiteral("cdl") || view == QStringLiteral("spice")) {
        viewItem->setIcon(0, QIcon(QStringLiteral(":/icons/spice.svg")));
    } else if (view == QStringLiteral("schematic") || view == QStringLiteral("symbol")) {
        viewItem->setIcon(0, QIcon(QStringLiteral(":/icons/schematic.svg")));
    } else if (view == QStringLiteral("emmodel")) {
        viewItem->setIcon(0, QIcon(QStringLiteral(":/icons/emsetup.svg")));
    } else if (view == QStringLiteral("layout") || view == QStringLiteral("gds")
               || view == QStringLiteral("oas") || view == QStringLiteral("oasis")
               || view == QStringLiteral("lstr") || view == QStringLiteral("core")) {
        viewItem->setIcon(0, QIcon(QStringLiteral(":/icons/layout.svg")));
    }
}

/*!******************************************************************************************************************
 * \brief Expands shell-style environment variables in a given path string.
 *******************************************************************************************************************/
QString MainWindow::expandShellVariables(const QString& path) const
{
    QString result = path;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    QRegularExpression re(R"(\$(\{([^}]+)\}|([A-Za-z_][A-Za-z0-9_]*)))");
    QRegularExpressionMatchIterator it = re.globalMatch(result);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString varName = match.captured(2).isEmpty() ? match.captured(3) : match.captured(2);
        QString varValue = env.value(varName);
        result.replace(match.captured(0), varValue);
    }

    return result;
}

/*!******************************************************************************************************************
 * \brief Resolves absolute or relative PROJECT path.
 *******************************************************************************************************************/
QString MainWindow::resolveProjectPath(const QString& projectsFile, const QString& rawPath)
{
    QString p = rawPath;
    p.replace('\\', '/');

    if (QDir::isAbsolutePath(p)) {
        return QDir::toNativeSeparators(p);
    }

    const QDir baseDir = QFileInfo(projectsFile).absoluteDir();
    return QDir::toNativeSeparators(baseDir.absoluteFilePath(p));
}

/*!******************************************************************************************************************
 * \brief Loads KLayout .lib file content into LibMan.
 *******************************************************************************************************************/
void MainWindow::loadProjectFile(const QString &fileName)
{
    const QString selectedLib = getCurrentLibraryName();
    const QString selectedGroup = getCurrentGroupName();

    LibFileParser parser;
    if(!parser.parseFile(fileName)) {
        if (!libmanAutomatedTestRun()) {
            QMessageBox::warning(this, tr("DOOR"),
                                 tr("Can not read lib file '%1':\n%2.")
                                     .arg(fileName)
                                     .arg(parser.errorString()));
        }
        error("Can not read lib file '" + fileName + "'.");
        return;
    }

    const LibFileData& data = parser.data();

    m_ui->treeLibs->clear();
    m_ui->listGroups->clear();
    m_ui->listViews->clear();

    m_ui->txtLibSearch->clear();
    m_ui->txtCatSearch->clear();
    m_ui->txtCellSearch->clear();
    m_ui->txtViewSearch->clear();

    clearLibraryViewProperties();
    clearAllTechLibraryAttaches();
    m_wildcardDefines.clear();
    clearLoadedPdks();

    const QDir projectDir = QFileInfo(fileName).absoluteDir();
    QSet<QString> loadedLibraries;

    for(const LibDefinition& def : data.definitions) {

        const QString libName = def.name.trimmed();
        QString libPath = expandShellVariables(def.path.trimmed());
        libPath.replace('\\', '/');

        QString storedPath = libPath;
        if (!QDir::isAbsolutePath(libPath)) {
            storedPath = QDir::fromNativeSeparators(libPath);
            libPath = QDir::toNativeSeparators(projectDir.absoluteFilePath(libPath));
        }

        if(libName.isEmpty()) {
            error(QString("Skipping library with empty name from '%1'.").arg(def.sourceFile));
            continue;
        }

        if(libPath.isEmpty()) {
            error(QString("Skipping library '%1' with empty path.").arg(libName));
            continue;
        }

        if(libdefine::isWildcardDefinePath(storedPath)) {
            m_wildcardDefines.append(qMakePair(libName, storedPath));

            const QString scanRoot = libdefine::wildcardScanRoot(projectDir.absolutePath(), storedPath);
            if(scanRoot.isEmpty()) {
                error(QString("Wildcard define path does not exist for '%1': %2").arg(libName, storedPath));
                continue;
            }

            const QString libRoot = libdefine::wildcardLibraryRoot(projectDir.absolutePath(), storedPath);
            if(!libRoot.isEmpty()) {
                setLibraryRootDirectory(libName, libRoot);
            }

            const QStringList expanded =
                libdefine::expandWildcardDefinePath(projectDir.absolutePath(), storedPath);
            if(expanded.isEmpty()) {
                error(QString("Wildcard define matched no ROOM files for '%1': %2")
                          .arg(libName, storedPath));
            }

            for(const QString &corePath : expanded) {
                QString groupName;
                QString viewName;
                if(!resolveCellViewFromPath(corePath, &groupName, &viewName)) {
                    error(QString("Skipping wildcard file with unknown view type: %1").arg(corePath));
                    continue;
                }
                if(groupName.isEmpty()) {
                    continue;
                }

                const QString key = getLibraryKeyPrefix() + libName + "/" + groupName + "/" + viewName;
                m_properties->set(key, corePath);
            }

            loadedLibraries.insert(libName);
            continue;
        }

        QFileInfo fi(libPath);

        if(!fi.exists()) {
            error(QString("Library path does not exist for '%1': %2").arg(libName, libPath));
            continue;
        }

        if (fi.isDir()) {
            QString groupName;
            QString viewName;
            if (resolveCellViewFromPath(fi.absoluteFilePath(), &groupName, &viewName)
                && isEmSetupViewName(viewName)
                && !groupName.isEmpty()) {
                const QString key = getLibraryKeyPrefix() + libName + "/" + groupName + "/" + viewName;
                m_properties->set(key, fi.absoluteFilePath());
                loadedLibraries.insert(libName);
                continue;
            }
            setLibraryRootDirectory(libName, libPath);
            loadedLibraries.insert(libName);
            continue;
        }

        if(!fi.isFile()) {
            error(QString("Library path is not a file for '%1': %2").arg(libName, libPath));
            continue;
        }

        QString groupName;
        QString viewName;
        if (!resolveCellViewFromPath(fi.absoluteFilePath(), &groupName, &viewName)) {
            error(QString("Skipping file with unknown view type: %1").arg(fi.absoluteFilePath()));
            continue;
        }

        if (groupName.isEmpty()) {
            error(QString("Skipping file with empty group name: %1").arg(fi.absoluteFilePath()));
            continue;
        }

        const QString key = getLibraryKeyPrefix() + libName + "/" + groupName + "/" + viewName;
        m_properties->set(key, fi.absoluteFilePath());

        loadedLibraries.insert(libName);
    }

    for(const LibAttach &attach : data.attaches) {
        addTechLibraryAttach(attach.libraryName, attach.techLibraryName);
    }

    for(const LibPdk &pdkEntry : data.pdks) {
        QString pdkPath = expandShellVariables(pdkEntry.path.trimmed());
        pdkPath.replace('\\', '/');
        if (!QDir::isAbsolutePath(pdkPath)) {
            pdkPath = QDir::toNativeSeparators(projectDir.absoluteFilePath(pdkPath));
        }
        DoorPdkInfo pdkInfo = loadDoorPdk(pdkPath, pdkEntry.name);
        QString err;
        if (!registerLoadedPdk(pdkInfo, &err)) {
            error(QString("PDK '%1': %2").arg(pdkEntry.name, err.isEmpty() ? pdkInfo.error : err));
            continue;
        }
        for (const DoorPdkLibrary &lib : pdkInfo.libraries) {
            loadedLibraries.insert(lib.name);
        }
        this->info(QString("PDK '%1' loaded from %2 (%3 libraries)%4")
                       .arg(pdkInfo.name,
                            pdkInfo.rootPath,
                            QString::number(pdkInfo.libraries.size()),
                            pdkInfo.descriptorPath.isEmpty()
                                ? QStringLiteral(" [auto-detect]")
                                : QStringLiteral(" via .door-pdk.json")),
                   false);
    }

    loadLibraries();
    applyPdkTreeGrouping();

    if (!selectedLib.isEmpty()) {
        for (int i = 0; i < m_ui->treeLibs->topLevelItemCount(); ++i) {
            QTreeWidgetItem *libItem = m_ui->treeLibs->topLevelItem(i);
            if (!libItem || libItem->text(0) != selectedLib) {
                continue;
            }
            m_ui->treeLibs->setCurrentItem(libItem);
            populateLibraryBrowser(selectedLib);
            break;
        }
    }

    if (!selectedLib.isEmpty() && !selectedGroup.isEmpty()) {
        for (int i = 0; i < m_ui->listGroups->count(); ++i) {
            QListWidgetItem *groupItem = m_ui->listGroups->item(i);
            if (!groupItem || groupItem->text() != selectedGroup) {
                continue;
            }
            m_ui->listGroups->setCurrentItem(groupItem);
            loadViews(selectedLib, selectedGroup);
            break;
        }
    }

    setRecentProject(fileName);

    m_currentProjFile = fileName;

    QString fileTitle = QFileInfo(m_currentProjFile).completeBaseName();
    QString fileSuffix = QFileInfo(m_currentProjFile).completeSuffix();

    if(!fileSuffix.isEmpty()) {
        fileTitle += "." + fileSuffix;
    }

    setWindowTitle(getAppTitle() + " (" + fileTitle + ")");
    setStateSaved();

    setupProjectFileWatcher(fileName);

    info(QString("Lib file '%1' has been loaded. %2 libraries found.")
             .arg(fileName)
             .arg(loadedLibraries.count()));
}

void MainWindow::clearLibraryViewProperties()
{
    if (!m_properties) {
        return;
    }

    const QString prefix = getLibraryKeyPrefix();
    const QMap<QString, PropertyItem *> props = m_properties->getMap();
    QStringList keysToRemove;

    for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
        if (it.key().startsWith(prefix, Qt::CaseInsensitive)) {
            keysToRemove.append(it.key());
        }
    }

    for (const QString &key : keysToRemove) {
        m_properties->remove(key);
    }
}

/*!*********************************************************************************************************************
 * \brief Finds a representative view file for the specified library.
 *
 * This function searches all LibMan property entries belonging to the given library
 * and returns the absolute path of the first existing view file found.
 *
 * The returned file path can be used in the project file as a reference entry for
 * the library. During project loading, LibMan will restore the full library content
 * by scanning the library root directory.
 *
 * \param libName     Name of the library.
 *
 * \return Absolute path to an existing view file, or an empty string if none was found.
 **********************************************************************************************************************/
QString MainWindow::findRepresentativeLibraryFile(const QString &libName) const
{
    if(libName.isEmpty()) {
        return QString();
    }

    const QMap<QString, PropertyItem*> propItems = m_properties->getMap();
    const QString prefix = getLibraryKeyPrefix() + libName + "/";

    for(auto it = propItems.constBegin(); it != propItems.constEnd(); ++it) {
        const QString key = it.key();
        if(!key.startsWith(prefix)) {
            continue;
        }

        const QString filePath = m_properties->get<QString>(key).trimmed();
        QFileInfo fi(filePath);
        if(fi.exists() && fi.isFile()) {
            return fi.absoluteFilePath();
        }
    }

    return QString();
}

/*!*********************************************************************************************************************
 * \brief Returns all current project file entries derived from registered library views.
 *
 * This function iterates through all LibMan library property entries and collects
 * pairs of:
 *   - library name
 *   - absolute view file path
 *
 * Each returned pair corresponds to one define(...) line that can be written
 * to the project file.
 *
 * Unlike getCurrentLibraries(), this function preserves multiple entries for the
 * same library name.
 *
 * \return List of (library name, absolute file path) pairs.
 **********************************************************************************************************************/
QList<QPair<QString, QString>> MainWindow::getProjectWildcardDefines() const
{
    return m_wildcardDefines;
}

bool MainWindow::hasWildcardDefineForLibrary(const QString &libraryName) const
{
    for(const QPair<QString, QString> &entry : m_wildcardDefines) {
        if(entry.first == libraryName) {
            return true;
        }
    }
    return false;
}

QStringList MainWindow::discoverCellNamesFromDisk(const QString &libraryName) const
{
    QStringList cells;
    const QString libRoot = getLibraryPath(libraryName);
    if(libRoot.isEmpty()) {
        return cells;
    }

    QDir libDir(libRoot);
    if(!libDir.exists()) {
        return cells;
    }

    const QFileInfoList subDirs = libDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for(const QFileInfo &subDir : subDirs) {
        const QString cellName = subDir.fileName();
        if(cellName.startsWith(QLatin1Char('.'))) {
            continue;
        }

        const QDir cellDir(subDir.absoluteFilePath());
        const QStringList cores = cellDir.entryList(QStringList() << QStringLiteral("*.room"), QDir::Files);
        for(const QString &coreName : cores) {
            QString groupName;
            QString viewName;
            const QString corePath = cellDir.filePath(coreName);
            if(!resolveCellViewFromPath(corePath, &groupName, &viewName)) {
                continue;
            }
            if(groupName == cellName) {
                cells.append(cellName);
                break;
            }
        }
    }

    cells.sort();
    cells.removeDuplicates();
    return cells;
}

QStringList MainWindow::discoverViewNamesFromDisk(const QString &libraryName,
                                                const QString &cellName) const
{
    QStringList views;
    const QString libRoot = getLibraryPath(libraryName);
    if(libRoot.isEmpty() || cellName.isEmpty()) {
        return views;
    }

    const QDir cellDir(QDir(libRoot).filePath(cellName));
    if(!cellDir.exists()) {
        return views;
    }

    const QFileInfoList coreFiles = cellDir.entryInfoList(QStringList() << QStringLiteral("*.room"),
                                                          QDir::Files,
                                                          QDir::Name);
    for(const QFileInfo &coreFile : coreFiles) {
        QString groupName;
        QString viewName;
        if(resolveCellViewFromPath(coreFile.absoluteFilePath(), &groupName, &viewName)
           && groupName == cellName
           && !viewName.isEmpty()
           && !views.contains(viewName)) {
            views << viewName;
        }
    }

    const QFileInfoList emDirs = cellDir.entryInfoList(QStringList() << QStringLiteral("*.emsetup"),
                                                       QDir::Dirs | QDir::NoDotAndDotDot,
                                                       QDir::Name);
    for (const QFileInfo &emDir : emDirs) {
        QString groupName;
        QString viewName;
        if (resolveCellViewFromPath(emDir.absoluteFilePath(), &groupName, &viewName)
            && groupName == cellName
            && isEmSetupViewName(viewName)
            && !views.contains(viewName)) {
            views << viewName;
        }
    }

    // Common layout suffixes beside ROOM (directory libraries).
    for (const QString &suffix : {QStringLiteral("gds"), QStringLiteral("oas"), QStringLiteral("lstr"),
                                  QStringLiteral("cdl"), QStringLiteral("spice")}) {
        const QString candidate = cellDir.filePath(cellName + QLatin1Char('.') + suffix);
        if (QFileInfo::exists(candidate) && !views.contains(suffix)) {
            views << suffix;
        }
    }

    views.sort();
    return views;
}

QList<QPair<QString, QString>> MainWindow::getCurrentProjectEntries() const
{
    QList<QPair<QString, QString>> entries;
    QSet<QString> coveredFiles;
    const QDir projectDir = m_currentProjFile.isEmpty()
        ? QDir()
        : QFileInfo(m_currentProjFile).absoluteDir();

    for(const QPair<QString, QString> &wildcard : m_wildcardDefines) {
        entries.append(wildcard);
        if(!projectDir.path().isEmpty()) {
            const QStringList expanded =
                libdefine::expandWildcardDefinePath(projectDir.absolutePath(), wildcard.second);
            for(const QString &path : expanded) {
                coveredFiles.insert(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
            }
        }
    }

    QSet<QString> librariesWithViews;
    const QSet<QString> pdkMembers = pdkMemberLibraryNames();

    const QMap<QString, PropertyItem*> propItems = m_properties->getMap();

    for(auto it = propItems.constBegin(); it != propItems.constEnd(); ++it) {
        const QString key = it.key();
        if(!key.toUpper().startsWith(getLibraryKeyPrefix())) {
            continue;
        }

        QString tail = key;
        tail.remove(getLibraryKeyPrefix());

        const int pos = tail.indexOf('/');
        if (pos < 0) {
            continue;
        }

        const QString libName = tail.left(pos).trimmed();
        if(libName.isEmpty() || pdkMembers.contains(libName)) {
            continue;
        }

        const QString filePath = m_properties->get<QString>(key).trimmed();
        QFileInfo fi(filePath);
        if(!fi.exists() || !fi.isFile()) {
            continue;
        }

        const QString absolutePath = QDir::toNativeSeparators(fi.absoluteFilePath());
        if(coveredFiles.contains(absolutePath)) {
            continue;
        }

        librariesWithViews.insert(libName);
        entries.append(qMakePair(libName, absolutePath));
    }

    for(auto it = propItems.constBegin(); it != propItems.constEnd(); ++it) {
        const QString key = it.key();
        if(!key.toUpper().startsWith(getLibraryKeyPrefix())) {
            continue;
        }

        QString tail = key;
        tail.remove(getLibraryKeyPrefix());
        if (tail.contains(QLatin1Char('/'))) {
            continue;
        }

        const QString libName = tail.trimmed();
        if (libName.isEmpty() || librariesWithViews.contains(libName) || pdkMembers.contains(libName)) {
            continue;
        }

        const QString rootPath = m_properties->get<QString>(key).trimmed();
        const QFileInfo rootInfo(rootPath);
        if (!rootInfo.exists() || !rootInfo.isDir()) {
            continue;
        }

        entries.append(qMakePair(libName, rootInfo.absoluteFilePath()));
    }

    return entries;
}

QList<QPair<QString, QString>> MainWindow::projectEntriesForEditor() const
{
    return getCurrentProjectEntries();
}

bool MainWindow::saveProjectEntriesToFile(const QString &fileName,
                                          const QList<QPair<QString, QString>> &entries,
                                          bool keepMissingPaths)
{
    if (fileName.isEmpty()) {
        return false;
    }

    const QString absFileName = QFileInfo(fileName).absoluteFilePath();

    if (m_projFileWatcher) {
        m_projFileWatcher->removePath(absFileName);
    }

    QFile file(absFileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (m_projFileWatcher && QFileInfo(absFileName).exists()) {
            m_projFileWatcher->addPath(absFileName);
        }

        if (!libmanAutomatedTestRun()) {
            QMessageBox::warning(this, tr("DOOR"),
                                 tr("Can not write to file '%1':\n%2.")
                                     .arg(absFileName)
                                     .arg(file.errorString()));
        }
        error("Can not write to file '" + absFileName + "'.");
        return false;
    }

    QTextStream out(&file);
    const QDir baseDir = QFileInfo(absFileName).absoluteDir();

    out << "# KLayout library definition file\n";
    out << "# Generated by DOOR\n\n";

    for (const auto &entry : entries) {
        const QString libName = entry.first.trimmed();
        QString filePath = entry.second.trimmed();

        if (libName.isEmpty() || filePath.isEmpty()) {
            continue;
        }

        if (libdefine::isWildcardDefinePath(filePath)) {
            const QString scanRoot = libdefine::wildcardScanRoot(baseDir.absolutePath(), filePath);
            if (scanRoot.isEmpty()) {
                if (!keepMissingPaths) {
                    error(QString("Skipping library entry '%1': wildcard path does not exist: %2")
                              .arg(libName, filePath));
                    continue;
                }
            }

            out << "define("
                << toLibStringLiteral(libName)
                << ", "
                << toLibStringLiteral(QDir::toNativeSeparators(filePath))
                << ");\n";
            continue;
        }

        const QString resolvedPath = QDir::isAbsolutePath(filePath)
            ? filePath
            : baseDir.absoluteFilePath(filePath);
        const QFileInfo fi(resolvedPath);
        if (!fi.exists()) {
            if (!keepMissingPaths) {
                error(QString("Skipping library entry '%1': path does not exist: %2")
                          .arg(libName, resolvedPath));
                continue;
            }

            const QString storedPath = QDir::isAbsolutePath(filePath)
                ? QDir::toNativeSeparators(baseDir.relativeFilePath(QDir::cleanPath(filePath)))
                : QDir::toNativeSeparators(filePath);
            out << "define("
                << toLibStringLiteral(libName)
                << ", "
                << toLibStringLiteral(storedPath)
                << ");\n";
            continue;
        }

        QString storedPath;
        if (fi.isDir()) {
            storedPath = QDir::isAbsolutePath(filePath)
                ? QDir::toNativeSeparators(baseDir.relativeFilePath(fi.absoluteFilePath()))
                : QDir::toNativeSeparators(filePath);
        }
        else if (fi.isFile()) {
            storedPath = QDir::isAbsolutePath(filePath)
                ? QDir::toNativeSeparators(baseDir.relativeFilePath(fi.absoluteFilePath()))
                : QDir::toNativeSeparators(filePath);
        }
        else {
            continue;
        }

        out << "define("
            << toLibStringLiteral(libName)
            << ", "
            << toLibStringLiteral(storedPath)
            << ");\n";
    }

    if (!m_loadedPdks.isEmpty()) {
        out << '\n';
        const QString baseAbs = QDir::cleanPath(baseDir.absolutePath());
        for (const DoorPdkInfo &pdk : m_loadedPdks) {
            const QString abs = QDir::cleanPath(pdk.rootPath);
            QString storedPath;
            if (abs.startsWith(baseAbs, Qt::CaseInsensitive)) {
                storedPath = QDir::toNativeSeparators(baseDir.relativeFilePath(abs));
            } else {
                storedPath = QDir::toNativeSeparators(abs);
            }
            out << "pdk("
                << toLibStringLiteral(pdk.name)
                << ", "
                << toLibStringLiteral(storedPath)
                << ");\n";
        }
    }

    const QList<QPair<QString, QString>> attaches = getTechLibraryAttaches();
    if(!attaches.isEmpty()) {
        out << '\n';
        for(const QPair<QString, QString> &attach : attaches) {
            out << "attach("
                << toLibStringLiteral(attach.first)
                << ", "
                << toLibStringLiteral(attach.second)
                << ");\n";
        }
    }

    file.close();

    if (m_projFileWatcher && QFileInfo(absFileName).exists()) {
        m_projFileWatcher->addPath(absFileName);
    }

    return true;
}

/*!******************************************************************************************************************
 * \brief Saves LibMan libraries into KLayout .lib file.
 *******************************************************************************************************************/
void MainWindow::saveProjectFile(const QString &fileName)
{
    if (fileName.isEmpty()) {
        return;
    }

    m_ignoreProjectFileChange = true;

    if (!saveProjectEntriesToFile(fileName, getCurrentProjectEntries())) {
        QTimer::singleShot(100, this, [this]() {
            m_ignoreProjectFileChange = false;
        });
        return;
    }

    const QString absFileName = QFileInfo(fileName).absoluteFilePath();
    m_currentProjFile = absFileName;

    QString fileTitle = QFileInfo(absFileName).completeBaseName();
    const QString fileSuffix = QFileInfo(absFileName).completeSuffix();
    if (!fileSuffix.isEmpty()) {
        fileTitle += "." + fileSuffix;
    }

    setWindowTitle(getAppTitle() + " (" + fileTitle + ")");
    setupProjectFileWatcher(absFileName);
    setRecentProject(absFileName);

    info(QString("Project '%1' has been saved.").arg(absFileName));
    setStateSaved();

    QTimer::singleShot(100, this, [this]() {
        m_ignoreProjectFileChange = false;
    });
}

/*!******************************************************************************************************************
 * \brief Converts QString into valid .lib string literal.
 *******************************************************************************************************************/
QString MainWindow::toLibStringLiteral(const QString& value)
{
    QString s = value;
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    s.replace("\n", "\\n");
    s.replace("\r", "\\r");
    s.replace("\t", "\\t");

    return "\"" + s + "\"";
}

namespace {

QString techAttachKey(const QString &libraryName)
{
    return QStringLiteral("TechAttach_") + libraryName;
}

} // namespace

QString MainWindow::techAttachPropertyKey(const QString &libraryName) const
{
    return techAttachKey(libraryName);
}

void MainWindow::clearAllTechLibraryAttaches()
{
    if(!m_properties) {
        return;
    }

    const QString prefix = QStringLiteral("TechAttach_");
    QStringList keysToRemove;
    const QMap<QString, PropertyItem *> props = m_properties->getMap();
    for(auto it = props.constBegin(); it != props.constEnd(); ++it) {
        if(it.key().startsWith(prefix)) {
            keysToRemove.append(it.key());
        }
    }

    for(const QString &key : keysToRemove) {
        m_properties->remove(key);
    }
}

QString MainWindow::getTechLibraryAttach(const QString &libraryName) const
{
    const QStringList techs = getTechLibraryAttachList(libraryName);
    return techs.isEmpty() ? QString() : techs.first();
}

QStringList MainWindow::getTechLibraryAttachList(const QString &libraryName) const
{
    QStringList techs;
    if(libraryName.isEmpty() || !m_properties) {
        return techs;
    }

    const QString key = techAttachKey(libraryName);
    if(!m_properties->exists(key)) {
        return techs;
    }

    const QString raw = m_properties->get<QString>(key).trimmed();
    for(const QString &part : raw.split(QRegularExpression(QStringLiteral("[;,]")), Qt::SkipEmptyParts)) {
        const QString tech = part.trimmed();
        if(!tech.isEmpty() && !techs.contains(tech)) {
            techs.append(tech);
        }
    }
    return techs;
}

void MainWindow::setTechLibraryAttach(const QString &libraryName, const QString &techLibraryName)
{
    if(libraryName.isEmpty() || !m_properties) {
        return;
    }

    const QString tech = techLibraryName.trimmed();
    if(tech.isEmpty()) {
        clearTechLibraryAttach(libraryName);
        return;
    }

    // Replaces the full attach list with a single tech library.
    m_properties->set(techAttachKey(libraryName), tech);
}

void MainWindow::addTechLibraryAttach(const QString &libraryName, const QString &techLibraryName)
{
    if(libraryName.isEmpty() || !m_properties) {
        return;
    }

    const QString tech = techLibraryName.trimmed();
    if(tech.isEmpty()) {
        return;
    }

    QStringList techs = getTechLibraryAttachList(libraryName);
    if(!techs.contains(tech)) {
        techs.append(tech);
    }
    m_properties->set(techAttachKey(libraryName), techs.join(QLatin1Char(';')));
}

void MainWindow::clearTechLibraryAttach(const QString &libraryName)
{
    if(libraryName.isEmpty() || !m_properties) {
        return;
    }

    m_properties->remove(techAttachKey(libraryName));
}

QList<QPair<QString, QString>> MainWindow::getTechLibraryAttaches() const
{
    QList<QPair<QString, QString>> attaches;
    if(!m_properties) {
        return attaches;
    }

    const QString prefix = QStringLiteral("TechAttach_");
    const QMap<QString, PropertyItem *> props = m_properties->getMap();
    for(auto it = props.constBegin(); it != props.constEnd(); ++it) {
        if(!it.key().startsWith(prefix)) {
            continue;
        }

        const QString libName = it.key().mid(prefix.size());
        if(libName.isEmpty()) {
            continue;
        }

        for(const QString &techLib : getTechLibraryAttachList(libName)) {
            attaches.append(qMakePair(libName, techLib));
        }
    }

    return attaches;
}

QStringList MainWindow::getDesignLibrariesUsingTech(const QString &techLibraryName) const
{
    QStringList designLibs;
    if(techLibraryName.isEmpty()) {
        return designLibs;
    }

    for(const QPair<QString, QString> &attach : getTechLibraryAttaches()) {
        if(attach.second == techLibraryName) {
            designLibs.append(attach.first);
        }
    }

    designLibs.sort();
    designLibs.removeDuplicates();
    return designLibs;
}

QStringList MainWindow::resolveTechLibraryCorePaths(const QString &techLibraryName) const
{
    QStringList paths;
    if(techLibraryName.isEmpty() || !m_properties) {
        return paths;
    }

    const QString rootKey = getLibraryKeyPrefix() + techLibraryName;
    QString rootPath;
    if(m_properties->exists(rootKey)) {
        rootPath = m_properties->get<QString>(rootKey).trimmed();
    }
    const QFileInfo rootInfo(rootPath);
    if(rootInfo.isFile() && rootInfo.suffix().compare(QStringLiteral("core"), Qt::CaseInsensitive) == 0) {
        paths.append(rootInfo.absoluteFilePath());
        return paths;
    }

    if(rootInfo.isDir()) {
        QDir dir(rootInfo.absoluteFilePath());
        const QString preferred = dir.filePath(techLibraryName + QStringLiteral(".room"));
        if(QFileInfo::exists(preferred)) {
            paths.append(QFileInfo(preferred).absoluteFilePath());
            return paths;
        }

        const QStringList cores = dir.entryList(QStringList() << QStringLiteral("*.room"),
                                                QDir::Files,
                                                QDir::Name);
        if(cores.size() == 1) {
            paths.append(QFileInfo(dir.filePath(cores.first())).absoluteFilePath());
            return paths;
        }

        for(const QString &name : cores) {
            paths.append(QFileInfo(dir.filePath(name)).absoluteFilePath());
        }
        if(!paths.isEmpty()) {
            paths.sort();
            return paths;
        }
    }

    const QString prefix = getLibraryKeyPrefix() + techLibraryName + QLatin1Char('/');
    const QMap<QString, PropertyItem *> props = m_properties->getMap();
    for(auto it = props.constBegin(); it != props.constEnd(); ++it) {
        if(!it.key().startsWith(prefix)) {
            continue;
        }

        const QString path = m_properties->get<QString>(it.key()).trimmed();
        if(path.endsWith(QStringLiteral(".room"), Qt::CaseInsensitive)) {
            paths.append(path);
        }
    }

    paths.sort();
    paths.removeDuplicates();

    if(!m_currentProjFile.isEmpty()) {
        const QDir libDir(QFileInfo(m_currentProjFile).absoluteDir().filePath(techLibraryName));
        if(libDir.exists()) {
            QDirIterator it(libDir.absolutePath(),
                            QStringList() << QStringLiteral("*.symbol.room"),
                            QDir::Files,
                            QDirIterator::Subdirectories);
            while(it.hasNext()) {
                const QString discovered = QDir::toNativeSeparators(it.next());
                if(!paths.contains(discovered)) {
                    paths.append(discovered);
                }
            }
            paths.sort();
            paths.removeDuplicates();
        }
    }

    return paths;
}

QString MainWindow::resolveTechLibraryCorePath(const QString &techLibraryName) const
{
    const QStringList paths = resolveTechLibraryCorePaths(techLibraryName);
    if(paths.isEmpty()) {
        return QString();
    }
    if(paths.size() == 1) {
        return paths.first();
    }

    return QStringLiteral("(%1 symbol cores)").arg(paths.size());
}

bool MainWindow::isSchematicLikeView(const QString &viewName) const
{
    const QString view = viewName.trimmed().toLower();
    if(view == QStringLiteral("schematic") || view == QStringLiteral("symbol")) {
        return true;
    }
    if(view == QStringLiteral("sch") || view == QStringLiteral("sym")) {
        return true;
    }
    if(view.endsWith(QStringLiteral(".room"))) {
        return view.contains(QStringLiteral("schematic")) || view.contains(QStringLiteral("symbol"));
    }

    return false;
}

void MainWindow::launchSchematicTool(const QString &tool, const QString &viewPath)
{
    if(tool.isEmpty() || viewPath.isEmpty()) {
        return;
    }

    const QString absViewPath = QFileInfo(viewPath).absoluteFilePath();
    // Prefer *.room for tools; rebuild from sibling *.json when JSON is newer / room missing.
    if(!ensureWorkingRoomFromJson(absViewPath)) {
        error(tr("Schematic file not found: %1").arg(viewPath), false);
        return;
    }
    if(absViewPath.isEmpty() || !QFileInfo::exists(absViewPath)) {
        error(tr("Schematic file not found: %1").arg(viewPath), false);
        return;
    }

    const QFileInfo toolFi(tool);
    const QString absToolPath = toolFi.isAbsolute() ? toolFi.absoluteFilePath() : tool;
    const bool isBatchFile =
        absToolPath.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive)
        || absToolPath.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive);

    if(isBatchFile && !QFileInfo::exists(absToolPath)) {
        error(tr("Schematic tool not found: %1").arg(tool), false);
        return;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString libName = getCurrentLibraryName();
    QString techLib = getTechLibraryAttach(libName);
    if(techLib.isEmpty() && !resolveTechLibraryCorePaths(libName).isEmpty()) {
        techLib = libName;
    }
    QStringList techLibs = getTechLibraryAttachList(libName);
    if(techLibs.isEmpty() && !techLib.isEmpty()) {
        techLibs.append(techLib);
    }
    QStringList nativePaths;
    // Design-library symbol cores (e.g. inverter.symbol.room — JSON or binary content).
    for(const QString &path : resolveTechLibraryCorePaths(libName)) {
        if(!path.endsWith(QStringLiteral(".symbol.room"), Qt::CaseInsensitive)) {
            continue;
        }
        const QString native = QDir::toNativeSeparators(path);
        if(!native.isEmpty() && !nativePaths.contains(native)) {
            nativePaths.append(native);
        }
    }
    if(!techLibs.isEmpty()) {
        for(const QString &tech : techLibs) {
            for(const QString &path : resolveTechLibraryCorePaths(tech)) {
                const QString native = QDir::toNativeSeparators(path);
                if(!native.isEmpty() && !nativePaths.contains(native)) {
                    nativePaths.append(native);
                }
            }
        }
    }
    if(!nativePaths.isEmpty()) {
        QStringList techEnv = techLibs;
        if(!libName.isEmpty() && !techEnv.contains(libName)) {
            techEnv.prepend(libName);
        }
        const QString listPath = QDir::temp().filePath(
            QStringLiteral("libman_primitive_libs_%1.txt").arg(QDateTime::currentMSecsSinceEpoch()));
        QFile listFile(listPath);
        if(listFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&listFile);
            for(const QString &path : nativePaths) {
                out << path << QLatin1Char('\n');
            }
            env.insert(QStringLiteral("CORE_PRIMITIVE_LIBS_FILE"), QDir::toNativeSeparators(listPath));
        } else {
            env.insert(QStringLiteral("CORE_PRIMITIVE_LIBS"), nativePaths.join(QLatin1Char(';')));
            env.insert(QStringLiteral("CORE_PRIMITIVE_LIB"), nativePaths.first());
        }
        env.insert(QStringLiteral("LIBMAN_TECH_LIBRARY"), techEnv.join(QLatin1Char(';')));
        env.insert(QStringLiteral("QUCS_PRIMITIVE_LIB"), QStringLiteral("IHP_PDK_nonlinear_components"));
    }

    QProcess proc;
    proc.setProcessEnvironment(env);

#ifdef Q_OS_WIN
    if(isBatchFile) {
        const QString workDir = QFileInfo(absToolPath).absolutePath();
        const QString nativeView = QDir::toNativeSeparators(absViewPath);
        const QString wslScriptWin = QDir(workDir).filePath(QStringLiteral("open-xschem-wsl.sh"));

        if(QFileInfo::exists(wslScriptWin)) {
            // Launch wsl.exe directly (no cmd.exe). CREATE_NO_WINDOW on cmd breaks WSLg;
            // Qt startDetached sets DETACHED_PROCESS which also hides GUI — clear it.
            auto toWslPath = [](const QString &winPath) -> QString {
                const QString abs = QDir::fromNativeSeparators(QFileInfo(winPath).absoluteFilePath());
                if(abs.size() >= 2 && abs.at(1) == QLatin1Char(':')) {
                    return QStringLiteral("/mnt/") + abs.at(0).toLower() + abs.mid(2);
                }
                return abs;
            };

            const QString commonDbRoot = QFileInfo(QDir(workDir).filePath(QStringLiteral("../../CommonDB")))
                                             .absoluteFilePath();
            if(QFileInfo::exists(commonDbRoot)) {
                env.insert(QStringLiteral("COMMONDB_ROOT"), QDir::toNativeSeparators(commonDbRoot));
            }

            env.insert(QStringLiteral("WSLENV"),
                       QStringLiteral(
                           "CORE_PRIMITIVE_LIBS_FILE/p:CORE_PRIMITIVE_LIBS/p:CORE_PRIMITIVE_LIB/p:"
                           "ROOM_PRIMITIVE_LIBS_FILE/p:ROOM_PRIMITIVE_LIBS/p:ROOM_PRIMITIVE_LIB/p:"
                           "LIBMAN_TECH_LIBRARY/u:XSCHEM_PRIMITIVE_CACHE/p:COMMONDB_ROOT/p:"
                           "XSCHEM_OPEN_CORE/p:XSCHEM_OPEN_ROOM/p:XSCHEM_OPEN_FILE/p:"
                           "PDK_ROOT/u:PDK/u:XSCHEM_NETLIST_DIR/u"));
            proc.setProcessEnvironment(env);
            const QString wslWorkDir = toWslPath(workDir);
            const QString wslView = toWslPath(absViewPath);
            // Non-login bash (no -l): sourcing ~/.bashrc on every open added seconds.
            // Pass the view as a real argv — do not wrap in bash -c/'...'.
            proc.setProgram(QStringLiteral("C:\\Windows\\System32\\wsl.exe"));
            proc.setArguments(QStringList()
                              << QStringLiteral("--cd")
                              << wslWorkDir
                              << QStringLiteral("bash")
                              << QStringLiteral("./open-xschem-wsl.sh")
                              << wslView);
            proc.setWorkingDirectory(workDir);
            proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
                args->flags &= ~static_cast<DWORD>(DETACHED_PROCESS);
                args->inheritHandles = false;
            });
        }
        else {
            proc.setProgram(QStringLiteral("cmd.exe"));
            proc.setArguments(QStringList()
                              << QStringLiteral("/c")
                              << QDir::toNativeSeparators(absToolPath)
                              << nativeView);
            proc.setWorkingDirectory(workDir);
            proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
                args->flags |= CREATE_NO_WINDOW;
                args->inheritHandles = false;
            });
        }
    }
    else {
        proc.setProgram(absToolPath);
        proc.setArguments(QStringList() << absViewPath);
        proc.setWorkingDirectory(toolFi.exists() ? toolFi.absolutePath() : QDir::currentPath());
    }
#else
    Q_UNUSED(isBatchFile);
    proc.setProgram(tool);
    proc.setArguments(QStringList() << absViewPath);
    proc.setWorkingDirectory(QDir::currentPath());
#endif

    qint64 pid = 0;
    if(!proc.startDetached(&pid)) {
        error(tr("Failed to start schematic tool:\n%1\n%2")
                  .arg(tool, absViewPath),
              false);
        return;
    }

    info(tr("Starting schematic tool: %1").arg(tool), false);
}

/*!******************************************************************************************************************
 * \brief Creates empty project file.
 *******************************************************************************************************************/
bool MainWindow::createNewFile(const QString &fileName)
{
    if(fileName.isEmpty()) {
        return false;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (!libmanAutomatedTestRun()) {
            QMessageBox::warning(this, tr("DOOR"),
                                 tr("Can not write to file '%1':\n%2.")
                                     .arg(fileName)
                                     .arg(file.errorString()));
        }
        error("Can not write to file '" + fileName + "'.");
        return false;
    }

    file.close();

    return true;
}
