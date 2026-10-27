#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "gds/gdsreader.h"
#include "oas/oasReader.h"
#include "room/roomcellreader.h"
#include "room/roomKlayoutBridge.h"
#include "room/room_path_utils.h"
#include "src/klayoutCellResolver.h"
#include "src/klayout_tools.h"
#include "src/door_pdk.h"

#include <QProcess>
#include <QMainWindow>

class QMimeData;
class QDropEvent;
class QJsonObject;
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QDateTime>
#include <memory>

class Properties;
class QTreeWidget;

#ifdef LIBMAN_TESTING
class MainWindowTestHooks;
#endif
class QListWidget;
class QListWidgetItem;
class QTreeWidgetItem;
class QFileSystemWatcher;
class SnapshotView;
class ProjectEditor;
class PythonConsole;
class QPlainTextEdit;
class AssistantChatPanel;
class AssistantMcp;
class AssistantAgent;
class RoomMcpClient;

namespace Ui {
class MainWindow;
}

/*!*********************************************************************************************************************
 * \brief The MainWindow class is responsible for the main LibMan UI and controls all slots and signals.
 **********************************************************************************************************************/
class MainWindow : public QMainWindow
{
    Q_OBJECT

    friend class NewView;
    friend class ProjectManager;
    friend class ProjectEditor;
    friend class ImportDialog;
    friend class ExportDialog;
    friend class CoreImportService;
    friend class CoreExportService;
#ifdef LIBMAN_TESTING
    friend class MainWindowTestHooks;
#endif

    /*!
     * \brief The RECENT_FILES enum specifies maximum number of recent project files.
     */
    enum RECENT_FILES {
        PROJ_MAX_COUNT           = 5
    };

    /*!
     * \brief The COPY_STATE enum specifies different states used for copying project/group/view (library/cell/view).
     */
    enum COPY_STATE {
        NONE                     = 1000,
        PROJECT,
        GROUP,
        VIEW
    };

    /*!
     * \brief Custom roles used in the views tree widget items.
     */
    enum VIEW_ITEM_ROLE {
        RoleType                 = Qt::UserRole + 1,    /*!< Item type: view root / cell node / etc. */
        RoleGdsPath              = Qt::UserRole + 2,    /*!< Absolute path to GDS file for "gds" view node. */
        RoleCellName             = Qt::UserRole + 3,    /*!< Cell name for hierarchy nodes. */
        RoleOasPath              = Qt::UserRole + 4,    /*!< Absolute path to OASIS file for "oas" view node. */
        RoleLStreamPath          = Qt::UserRole + 5,    /*!< Absolute path to LStream file for "lstream" view node. */
        RoleDocumentPath         = Qt::UserRole + 6,    /*!< Absolute path to document (pdf/txt/etc). */
        RoleCorePath             = Qt::UserRole + 7,    /*!< Absolute path to ROOM (.room) file. */
        RoleEmSetupPath          = Qt::UserRole + 8,    /*!< Absolute path to <cell>.emsetup/ folder. */
        RolePdkPath              = Qt::UserRole + 9     /*!< Absolute PDK root for ItemPdkRoot. */
    };

    /*!
     * \brief Item types stored in RoleType.
     */
    enum VIEW_ITEM_TYPE {
        ItemViewGds              = 1,                   /*!< "gds" view root node. */
        ItemCell                 = 2,                   /*!< Cell node in hierarchy (GDS/OASIS). */
        ItemViewOas              = 3,                   /*!< "oas" view root node. */
        ItemViewLStream          = 4,                   /*!< "lstr" view root node. */
        ItemViewCore             = 5,                   /*!< "core" view root node. */
        ItemViewEmSetup          = 6,                   /*!< "emsetup" folder view root. */
        ItemEmSetupVariant       = 7,                   /*!< Variant under emsetup (e.g. nominal). */
        ItemPdkRoot              = 8                    /*!< Expandable PDK node in Projects tree. */
    };

public:
    explicit MainWindow(const QString &projFile, const QString &runDir, QWidget *parent = 0);
    ~MainWindow();

    /*!
     * \brief Cache entry storing parsed GDS hierarchy for a given file.
     */
    struct GdsCacheEntry
    {
        bool                                loaded   = false;    /*!< True if hierarchy is successfully loaded. */
        bool                                loading  = false;    /*!< True while async loading is in progress. */
        QString                             path;                /*!< Absolute path to the GDS file. */
        GdsReader::GdsHierarchy             hierarchy;            /*!< Parsed hierarchy (cells + references). */
        QStringList                         errors;              /*!< Errors collected during parsing. */
    };

    /*!
     * \brief Cache entry storing parsed OASIS hierarchy for a given file.
     */
    struct OasCacheEntry
    {
        bool                                loaded   = false;    /*!< True if hierarchy is successfully loaded. */
        bool                                loading  = false;    /*!< True while async loading is in progress. */
        QString                             path;                /*!< Absolute path to the OASIS file. */
        LayoutHierarchy                     hierarchy;            /*!< Parsed hierarchy (cells + placements). */
        QStringList                         errors;              /*!< Errors collected during parsing. */
    };

    /*!
     * \brief Cache entry storing parsed LStream (Capt'n Proto) hierarchy for a given file.
     */
    struct LStreamCacheEntry
    {
        QString path;
        QStringList cellNames;

        bool loaded = false;
        bool loading = false;

        QStringList errors;
    };

    /*!
     * \brief Cache entry storing parsed ROOM hierarchy for a given .room file.
     */
    struct CoreCacheEntry
    {
        bool                                loaded   = false;
        bool                                loading  = false;
        QString                             path;
        RoomCellReader::CoreHierarchy       hierarchy;
        QStringList                         errors;
    };

    /*!
     * \brief Per-item spinner animation state.
     */
    struct SpinnerState
    {
        QTimer                             *timer    = nullptr;  /*!< Timer driving icon rotation. Owned by MainWindow. */
        int                                 angleDeg = 0;        /*!< Current rotation angle (0..359). */
    };

    /*! Pending first layout for two-step XOR (empty klayoutPath = none). */
    struct XorSelection
    {
        QString                             libName;
        QString                             groupName;
        QString                             viewName;
        QString                             displayLabel;
        QString                             viewPath;
        QString                             klayoutPath;
        QString                             cellName;
    };

private slots:
    void                                closeEvent(QCloseEvent *event) override;
    bool                                eventFilter(QObject *obj, QEvent *event) override;

    void                                onProjectFileChanged(const QString &path);
    void                                onCoreLockWatcherChanged(const QString &path);
    void                                on_treeLibs_itemSelectionChanged();

    void                                showViewMenu(const QPoint &pos);
    void                                showGroupMenu(const QPoint &pos);
    void                                showLibraryMenu(const QPoint &pos);
    void                                showCategoryMenu(const QPoint &pos);
    void                                showMessagesMenu(const QPoint &pos);

    void                                clearMessages();
    void                                onLogAnchorClicked(const QUrl &url);

    void                                on_viewItemExpanded(QTreeWidgetItem *item);
    void                                renameSelectedLibrary();
    void                                addExistingCell();
    void                                attachTechLibrary();
    void                                detachTechLibrary();

    void                                addNewGdsView();
    void                                addNewOasView();
    void                                addNewLStreamView();
    void                                addNewCoreView();
    void                                addNewCoreSchematicView();
    void                                addNewCoreSymbolView();
    void                                addNewEmSetupView();
    void                                attachEmSetupModel();
    void                                renameEmSetupVariant();
    void                                copyEmSetupSelection();

    void                                addNewGroup();
    void                                addNewProject();
    void                                addNewCategory();
    void                                addNewSpiceView();
    void                                addNewLayoutView();
    void                                addNewSchematicView();

    void                                removeSelectedView();
    void                                removeSelectedGroup();
    void                                removeSelectedProject();
    void                                removeSelectedCategory();

    void                                showViewInfo();
    void                                convertSelectedViewToJson();
    void                                convertSelectedViewToRoom();
    void                                convertSelectedGroupsToRoom();
    bool                                cellHasRoomSchematicOrSymbol(const QString &libName,
                                                                     const QString &cellName) const;
    void                                showGroupInfo();
    void                                showProjectInfo();
    void                                showCategoryInfo();

    void                                removeFromGroup();
    void                                removeGroupUnion();
    void                                showFolderInfo(const QString &, const QString &, const QString &, bool clear = true, const QString &extraLines = QString());
    void                                mergeProjectIntoGroup();

    void                                pasteSelectedData();
    void                                copySelectedView();
    void                                copySelectedGroup();
    void                                copySelectedProject();
    void                                clearCurrentCopyState();
    void                                addViewToBeCopied(const QString &);
    void                                addProjectToBeCopied(const QString &);
    void                                addGroupToBeCopied(const QString &, const QString &);

    void                                loadRecentProject();
    void                                openMostRecentProject();
    void                                updateRecentProjectActions();
    void                                applyKeyBindings();
    void                                loadProjectFile(const QString &);
    void                                saveProjectFile(const QString &);
    QList<QPair<QString, QString>>      projectEntriesForEditor() const;
    bool                                saveProjectEntriesToFile(const QString &fileName,
                                                                 const QList<QPair<QString, QString>> &entries,
                                                                 bool keepMissingPaths = false);
    void                                setRecentProject(const QString &);
    QString                             resolveProjectPath(const QString& projectsFile, const QString& rawPath);
    QString                             findRepresentativeLibraryFile(const QString &libName) const;
    QList<QPair<QString, QString>>      getCurrentProjectEntries() const;

    void                                gitShowStatus();
    void                                gitCommitChanges();
    void                                gitShowLog();
    void                                gitShowDiff();
    void                                gitPull();
    void                                gitPush();
    void                                gitCheckout();

    void                                markXorFirstLayout();
    void                                runXorWithSelectedLayout();
    void                                clearXorSelection();

    void                                on_actionSave_triggered();
    void                                on_actionSave_As_triggered();
    void                                on_actionExit_triggered();
    void                                on_actionShow_Categories_toggled(bool);
    void                                on_actionShow_Documents_toggled(bool);
    void                                on_actionShow_Snapshot_toggled(bool);
    void                                on_actionShow_Messages_toggled(bool);
    void                                on_actionShow_Command_Line_toggled(bool);
    void                                on_actionShow_Assistant_toggled(bool);
    void                                refreshSnapshot();
    void                                runPythonCommand();
    void                                onPythonConsoleOutput(const QString &text);
    void                                onPythonConsoleStarted(const QString &detail);
    void                                onPythonConsoleFailed(const QString &reason);
    void                                onPythonCommandFinished(bool ok);
    void                                onPythonLibmanCommand(const QJsonObject &cmd);

    void                                on_actionTools_triggered();
    void                                on_actionProjects_triggered();
    void                                on_actionKeyBindings_triggered();
    void                                on_actionOpenMostRecent_triggered();
    void                                on_actionOpen_triggered();
    void                                on_actionEditProject_triggered();
    void                                on_actionImport_triggered();
    void                                on_actionExport_triggered();
    void                                on_actionClear_Recent_File_Stack_triggered();

    void                                on_treeLibs_itemClicked(QTreeWidgetItem *item, int column);
    void                                on_listViews_itemDoubleClicked(QTreeWidgetItem *item, int);
    void                                on_listDocumentation_itemDoubleClicked(QTreeWidgetItem *item);
    void                                on_listGroups_itemClicked(QListWidgetItem *item);
    void                                on_listCategories_itemClicked(QTreeWidgetItem *item);
    void                                on_treeLibs_itemChanged(QTreeWidgetItem *item, int column);
    void                                on_listCategories_itemDoubleClicked(QTreeWidgetItem *item, int column);

    void                                on_txtLibSearch_textEdited(const QString &arg1);
    void                                on_txtCatSearch_textEdited(const QString &arg1);
    void                                on_txtCellSearch_textEdited(const QString &arg1);
    void                                on_txtViewSearch_textEdited(const QString &arg1);

    void                                on_actionAbout_triggered();
    void                                on_actionProject_triggered();
    void                                on_actionGroup_triggered();
    void                                on_actionUnion_triggered();
    void                                on_actionCategory_triggered();
    void                                on_actionSession_triggered();

    void                                on_actionReload_triggered();

private:
    void                                loadSettings();
    void                                warmWslForXschemIfNeeded();
    void                                updateMessagesPanelVisibility();
    void                                setupPythonConsole();
    void                                setupAssistant();
    void                                registerAssistantMcpTools();
    void                                configureAssistantAgent();
    void                                persistAssistantSettings();
    bool                                registerLoadedPdk(const DoorPdkInfo &pdk, QString *errorOut = nullptr);
    /*! Reload an already-registered PDK from disk (after editing .door-pdk.json). */
    bool                                reloadLoadedPdk(const QString &pdkName, QString *errorOut = nullptr);
    void                                editPdkDescriptor(const QString &pdkName, const QString &pdkRoot);
    void                                clearLoadedPdks();
    void                                applyPdkTreeGrouping();
    bool                                libraryHasIndexedViews(const QString &libName) const;
    void                                indexPdkLibraryContent(const QString &libName, const QString &libRoot);
    bool                                tryAddDirectoryAsPdk(const QString &dirPath);
    QSet<QString>                       pdkMemberLibraryNames() const;
    const DoorPdkInfo                  *activePdk() const;
    QString                             resolvePdkRoot() const;
    QString                             resolvePdkName() const;
    QString                             resolvePdkPath() const;
    QJsonObject                         buildPdkStateObject() const;
    QJsonObject                         buildTechInfoObject() const;
    void                                appendPythonLog(const QString &text, bool isError = false);
    void                                pushPythonHistory(const QString &code);
    bool                                recallPythonHistory(int delta);

    void                                info(const QString &msg, bool clear = true);
    void                                error(const QString &msg, bool clear = true);
    void                                appendLogFileLink(const QString &filePath, const QString &label = QString());
    void                                openLayoutFileInKLayout(const QString &layoutPath, const QString &cellName = QString());

    void                                initIcons();

    void                                initRecentProjectMenu();

    bool                                createNewFile(const QString &);
    bool                                isStateChanged() const;
    QString                             toLibStringLiteral(const QString& value);

    void                                setStateSaved();
    void                                setStateChanged();

    QString                             getCurrentProjectDirectory() const;
    QStringList                         findLibraryDocuments(const QString &libraryDir) const;

    void                                setupProjectFileWatcher(const QString &projFile);
    void                                clearCurrentProjectData();
    void                                clearLibraryViewProperties();
    void                                reloadProjectFileFromDisk();

    bool                                filterViewsTreeItemNoPopulate(QTreeWidgetItem *item, const QString &filter);

    bool                                registerCreatedView(const QString &libName,
                                                            const QString &groupName,
                                                            const QString &viewName,
                                                            const QString &viewPath);

    /*! Register/unregister a view from CML ``define()`` / ``undefine()`` without reload dialog. */
    bool                                registerViewFromCml(const QString &libName, const QString &viewPath);
    bool                                unregisterViewFromCml(const QString &libName, const QString &viewPath);
    void                                ensureLibraryInTree(const QString &libName);
    void                                ensureGroupInList(const QString &groupName);
    void                                ensureViewInList(const QString &libName,
                                                         const QString &groupName,
                                                         const QString &viewName,
                                                         const QString &viewPath);
    void                                saveProjectFileQuiet(const QString &fileName);

    void                                loadGdsHierarchyAsync(const QString &gdsPath,
                                                              const std::shared_ptr<GdsCacheEntry> &entry,
                                                              QTreeWidgetItem *targetItem,
                                                              const QString &requestedCellName = QString());

    void                                loadOasHierarchyAsync(const QString &oasPath,
                                                              const std::shared_ptr<OasCacheEntry> &entry,
                                                              QTreeWidgetItem *targetItem,
                                                              const QString &requestedCellName = QString());

    void                                setLoadingSpinner(QTreeWidgetItem *item, bool on);

    std::shared_ptr<GdsCacheEntry>      ensureGdsLoaded(const QString &gdsPath);
    void                                populateGdsTopLevel(QTreeWidgetItem *gdsItem,
                                                            const std::shared_ptr<GdsCacheEntry> &entry);
    void                                populateCellChildren(QTreeWidgetItem *cellItem,
                                                             const std::shared_ptr<GdsCacheEntry> &entry,
                                                             const QString &cellName);

    std::shared_ptr<OasCacheEntry>      ensureOasLoaded(const QString &oasPath);
    void                                populateOasTopLevel(QTreeWidgetItem *oasItem,
                                                            const std::shared_ptr<OasCacheEntry> &entry);
    void                                populateOasCellChildren(QTreeWidgetItem *cellItem,
                                                                const std::shared_ptr<OasCacheEntry> &entry,
                                                                const QString &cellName);

    std::shared_ptr<LStreamCacheEntry>  ensureLStreamLoaded(const QString &path);
    void                                loadLStreamAsync(const QString &path,
                                                         const std::shared_ptr<LStreamCacheEntry> &entry,
                                                         QTreeWidgetItem *targetItem);
    void                                populateLStreamTopLevel(QTreeWidgetItem *item,
                                                                const std::shared_ptr<LStreamCacheEntry> &entry);

    void                                loadRoomHierarchyAsync(const QString &corePath,
                                                               const std::shared_ptr<CoreCacheEntry> &entry,
                                                               QTreeWidgetItem *targetItem,
                                                               const QString &requestedCellName = QString());

    std::shared_ptr<CoreCacheEntry>     ensureCoreLoaded(const QString &corePath);
    void                                populateCoreTopLevel(QTreeWidgetItem *coreItem,
                                                             const std::shared_ptr<CoreCacheEntry> &entry);
    void                                populateCoreCellChildren(QTreeWidgetItem *cellItem,
                                                                 const std::shared_ptr<CoreCacheEntry> &entry,
                                                                 const QString &cellName);

    void                                checkAndSaveProjectData(QCloseEvent *);

    void                                createProjectUnionMenu();

    void                                loadLibraries();
    void                                populateLibraryBrowser(const QString &libName);
    void                                loadGroups(const QString &libPath);
    void                                loadDocuments(const QString &libPath);
    void                                loadCategories(const QString &libPath);
    void                                loadCombinedLibs(const QMap<QString, QStringList> &);
    void                                loadViews(const QString &libPath, const QString &groupName);

    void                                clearLibrarySelectionDependentViews();
    void                                updateLibraryActionStates();
    void                                setupDragAndDrop();

    QStringList                         collectSupportedViewSuffixes() const;
    bool                                isSupportedViewDrop(const QMimeData *mimeData) const;
    bool                                importCellViewFile(const QString &libName, const QString &srcFilePath);
    bool                                registerCellViewAtPath(const QString &libName,
                                                               const QString &cellName,
                                                               const QString &viewName,
                                                               const QString &filePath);
    void                                handleViewFileDrop(QDropEvent *event);
    void                                setLibraryRootDirectory(const QString &libName, const QString &dirPath);

    void                                hideTreeItem(QTreeWidget *, const QString &filter);
    void                                hideListItem(QListWidget *, const QString &filter);
    bool                                filterTreeItem(QTreeWidgetItem *item, const QString &filter);

    bool                                isViewCopied() const;
    bool                                isGroupCopied() const;
    bool                                isProjectCopied() const;

    bool                                askForFileReplacement() const;
    bool                                askForPermanentDelete() const;
    bool                                promptDeleteChoice(bool *permanent) const;
    bool                                askUserForAction(const QString &title) const;

    void                                removeLibraryPropertyKeys(const QString &libName);
    void                                removeCellPropertyKeys(const QString &libName, const QString &groupName);

    bool                                removeDir(const QString &) const;
    void                                copyDir(const QString &, const QString &) const;

    QString                             getLibraryPath(const QString &) const;
    QString                             getLibraryKeyPrefix() const;

    QString                             getProjectFileFromDir(const QString &) const;
    QString                             expandShellVariables(const QString &path) const;
    QString                             detectViewFromPath(const QString& filePath) const;
    bool                                resolveCellViewFromPath(const QString &filePath,
                                                                  QString *groupName,
                                                                  QString *viewName) const;
    void                                configureCoreViewTreeItem(QTreeWidgetItem *viewItem,
                                                                  const QString &viewName,
                                                                  const QString &viewPath) const;
    void                                configureEmSetupViewTreeItem(QTreeWidgetItem *viewItem,
                                                                     const QString &viewPath) const;
    void                                applyViewTreeIcon(QTreeWidgetItem *viewItem, const QString &viewName) const;
    QString                             preferredLayoutPathForEmSetup(const QString &libName,
                                                                      const QString &cellName) const;
    QString                             writeEmSetupModelTemplate(const QString &modelPath,
                                                                  const QString &cellName,
                                                                  const QString &layoutPath,
                                                                  QString *errorMsg = nullptr) const;
    QTreeWidgetItem *                   findEmSetupViewItem() const;
    void                                refreshEmSetupVariantItems(QTreeWidgetItem *emItem) const;
    bool                                copyEmSetupModelBundle(const QString &sourceModelPy,
                                                               const QString &destVariantDir,
                                                               QString *errorMsg = nullptr) const;
    bool                                pickTargetLibraryAndCell(QString *libOut,
                                                                 QString *cellOut,
                                                                 const QString &dialogTitle);
    bool                                ensureEmSetupViewRegistered(const QString &libName,
                                                                    const QString &cellName,
                                                                    const QString &emRoot);
    QString                             uniqueEmSetupVariantName(const QString &emRoot,
                                                                 const QString &preferred) const;
    void                                applyCoreViewLockPresentation(QTreeWidgetItem *viewItem,
                                                                      const QString &viewName,
                                                                      const QString &viewPath);
    void                                syncCoreLockWatches();
    void                                refreshCoreViewLockItems(const QString &changedPath = QString());
    void                                scheduleCoreLockRefresh();
    /*! Reload View tree when disk views for the current cell differ (e.g. new .emmodel.room). */
    void                                maybeReloadCurrentCellViews();
    QString                             coreLockInfoExtraLines(const QString &corePath) const;
    void                                createCoreView(const QString &viewName);

    QString                             generateCopyName(const QString &, const QString &, const QString &suffix = "") const;

    QString                             getViewPath(const QString &, const QString &, const QString &) const;

    QString                             getCurrentViewName() const;
    QString                             getCurrentGroupName() const;
    QString                             getCurrentUnionName() const;
    QString                             getCurrentWorkingDir() const;
    QString                             getCurrentLibraryName() const;
    QStringList                         registeredLibraryNames() const;
    QString                             getCurrentProjectFile() const;
    QString                             getCurrentLibraryPath() const;
    QString                             getCurrentCategoryName() const;

    QString                             getCurrentGitPathForItem() const;

    QString                             getCurrentViewFilePath(const QString &) const;
    QString                             getCurrentDocumentFilePath(const QString &) const;
    QString                             getCurrentGroupPath(const QString &viewName, bool toBeCreated = false);

    QString                             getSettingsHeaderName() const;
    QString                             getToolByView(const QString &) const;
    QString                             getDocumentTool(const QString &) const;

    QString                             getAppTitle() const;
    /** @deprecated Use getAppTitle(). */
    QString                             getLibManTitle() const { return getAppTitle(); }

    QStringList                         getValidViewList() const;
    QStringList                         getCurrentGroups(const QString &) const;
    QStringList                         getCurrentViews(const QString &, const QString &) const;
    QStringList                         readLibraryCategories(const QString &, const QString &);

    QTreeWidgetItem*                    getTreeItemByName(const QString &name);

    QMap<QString, QString>              getCurrentLibraries() const;

    QString                             techAttachPropertyKey(const QString &libraryName) const;
    QString                             getTechLibraryAttach(const QString &libraryName) const;
    QStringList                         getTechLibraryAttachList(const QString &libraryName) const;
    void                                setTechLibraryAttach(const QString &libraryName, const QString &techLibraryName);
    void                                addTechLibraryAttach(const QString &libraryName, const QString &techLibraryName);
    void                                clearTechLibraryAttach(const QString &libraryName);
    QList<QPair<QString, QString>>      getProjectWildcardDefines() const;
    bool                                hasWildcardDefineForLibrary(const QString &libraryName) const;
    QStringList                         discoverCellNamesFromDisk(const QString &libraryName) const;
    QStringList                         discoverViewNamesFromDisk(const QString &libraryName,
                                                                  const QString &cellName) const;

    QList<QPair<QString, QString>>      getTechLibraryAttaches() const;
    QStringList                         getDesignLibrariesUsingTech(const QString &techLibraryName) const;
    void                                clearAllTechLibraryAttaches();
    QStringList                         resolveTechLibraryCorePaths(const QString &techLibraryName) const;
    QString                             resolveTechLibraryCorePath(const QString &techLibraryName) const;
    bool                                isSchematicLikeView(const QString &viewName) const;
    QString                             symbolCoreForCell(const QString &cellName) const;
    /*! Legacy sibling ``*.json`` path (cleanup only; format lives inside ``*.room``). */
    static QString                      siblingRoomJsonPath(const QString &roomPath);
    /*! View file exists (JSON or binary content inside ``*.room``). */
    bool                                ensureWorkingRoomFromJson(const QString &roomPath);
    /*! Overwrite ``*.room`` in place with room-json text. */
    bool                                convertRoomViewToJson(const QString &roomPath);
    /*! Overwrite JSON-in-``*.room`` in place with Cap’n Proto binary. */
    bool                                convertJsonSiblingToRoom(const QString &roomPath);
    void                                launchSchematicTool(const QString &tool, const QString &viewPath);

    bool                                ensureKLayoutServerRunning(const QString &tool);
    bool                                isKLayoutServerRunning() const;
    bool                                sendKLayoutOpenRequest(const QString &gdsPath, const QString &cellName);
    bool                                sendKLayoutSelectRequest(const QString &gdsPath, const QString &cellName);

    bool                                resolveSelectedLayoutForXor(QString *viewName,
                                                                    QString *viewPath,
                                                                    QString *klayoutPath,
                                                                    QString *cellName,
                                                                    QStringList *errors) const;
    bool                                isLayoutXorMenuCandidate(QTreeWidgetItem *item) const;

    QMap<QString, QStringList>          getSupportedViewsByTool() const;
    QStringList                         layoutViewsForTool(const QString &toolName) const;
    QString                             layoutPathForKLayout(const QString &viewName,
                                                             const QString &viewPath,
                                                             QStringList *errors = nullptr) const;
    QString                             preferredKLayoutCellForRoot(const QString &layoutPath,
                                                                  const QString &groupName) const;
    bool                                isLayoutViewTreeItem(QTreeWidgetItem *item) const;
    bool                                updateProjectFileLibraryName(const QString &oldName,
                                                                     const QString &newName);

private:
    Ui::MainWindow                     *m_ui                   = nullptr;  /*!< Main window UI instance (Qt Designer). */
    Properties                         *m_properties           = nullptr;  /*!< Properties collection with all settings. */

    bool                                m_isStateChanged       = false;    /*!< True if project state differs from last saved state. */

    QString                             m_itemText;                        /*!< Name of the last selected item. */
    QString                             m_runDirectory;                    /*!< Directory where LibMan was started. */
    QString                             m_currentProjFile;                 /*!< Currently loaded project file path. */

    QList<QAction*>                     m_recentProjects;                  /*!< Recent project actions list (menu). */

    QStringList                         m_copyData;                        /*!< Copy buffer: [name, path, ...] depending on COPY_STATE. */
    COPY_STATE                          m_currentCopyState     = NONE;     /*!< Current copy state (project/group/view). */

    XorSelection                        m_xorFirst;                        /*!< First layout picked for XOR comparison. */

    KLayoutTools                        m_klayoutTools;                    /*!< KLayout server, open scripts, and XOR. */

    QHash<QTreeWidgetItem*, SpinnerState> m_spinnerStates;                 /*!< Spinner animation state per tree item. */

    QFileSystemWatcher                  *m_projFileWatcher = nullptr;      /*!< Watches current project file for external modifications (edit/replace/remove). */
    QFileSystemWatcher                  *m_coreLockWatcher = nullptr;      /*!< Watches cell dirs / ROOM .lck sidecars (locks + new views). */
    bool                                m_ignoreProjectFileChange = false; /*!< Suppresses watcher reaction during internal save operations. */
    bool                                m_reloadingCellViews = false;      /*!< Guards loadViews against recursive watcher reloads. */
    QString                             m_emModelWatchPath;                /*!< Last watched *.emmodel.room path for the current cell. */
    QDateTime                           m_emModelWatchMtime;               /*!< Last known mtime of m_emModelWatchPath. */
    QSet<QString>                       m_coreLockWatchedDirs;             /*!< Cell directories watched for .lck / new *.room views. */
    QSet<QString>                       m_coreLockWatchedFiles;            /*!< Existing .core.lck files watched for changes/removal. */
    QTimer                              *m_coreLockRefreshTimer = nullptr; /*!< Debounced refresh for Windows watcher races. */
    QTimer                              *m_coreLockPollTimer = nullptr;    /*!< Periodic stale-lock cleanup (WSL PID checks). */
    QList<QPair<QString, QString>>      m_wildcardDefines;               /*!< define() entries using trailing /* wildcards. */
    QList<DoorPdkInfo>                  m_loadedPdks;                    /*!< PDK roots from pdk() / Add PDK. */

    QHash<QString, std::shared_ptr<GdsCacheEntry>> m_gdsCache;             /*!< GDS hierarchy cache: abs path -> entry. */
    QHash<QString, std::shared_ptr<OasCacheEntry>> m_oasCache;             /*!< OASIS hierarchy cache: abs path -> entry. */
    QHash<QString, std::shared_ptr<LStreamCacheEntry>> m_lstreamCache;     /*!< LStream hierarchy cache: abs path -> entry. */
    QHash<QString, std::shared_ptr<CoreCacheEntry>> m_coreCache;           /*!< ROOM hierarchy cache: abs path -> entry. */
    SnapshotView                       *m_snapshotView = nullptr;         /*!< Schematic/symbol thumbnail under the view list. */
    ProjectEditor                      *m_projectEditor = nullptr;        /*!< .projects editor pane in the main splitter. */
    PythonConsole                      *m_pythonConsole = nullptr;        /*!< Persistent Python REPL with ROOM preloaded. */
    QStringList                         m_pythonHistory;                  /*!< Command Line history (newest at end). */
    int                                 m_pythonHistoryIndex = -1;        /*!< Browse index; -1 = editing a new/draft line. */
    QString                             m_pythonHistoryDraft;             /*!< Text saved when Up starts history browse. */

    AssistantChatPanel                 *m_assistantChat = nullptr;        /*!< Bottom-right LLM / MCP assistant chat. */
    AssistantMcp                       *m_assistantMcp = nullptr;         /*!< In-process MCP tool registry. */
    AssistantAgent                     *m_assistantAgent = nullptr;       /*!< OpenAI-compatible LLM agent. */
    RoomMcpClient                      *m_roomMcp = nullptr;              /*!< Stdio client for Room ``python -m room.mcp_server``. */
};

/*!*******************************************************************************************************************
 * \brief Returns prefix for storing libraries in the LibMan settings collection.
 ********************************************************************************************************************/
inline QString MainWindow::getLibraryKeyPrefix() const
{
    return "LIBRARY_";
}

/*!*******************************************************************************************************************
 * \brief Returns header name used for storing LibMan settings.
 ********************************************************************************************************************/
inline QString MainWindow::getSettingsHeaderName() const
{
    return "LIBAMN";
}

/*!*******************************************************************************************************************
 * \brief Returns true if the LibMan window state has been changed.
 ********************************************************************************************************************/
inline bool MainWindow::isStateChanged() const
{
    return m_isStateChanged;
}

/*!*******************************************************************************************************************
 * \brief Returns path to the current project file.
 ********************************************************************************************************************/
inline QString MainWindow::getCurrentProjectFile() const
{
    return m_currentProjFile;
}

/*!*******************************************************************************************************************
 * \brief Returns true if a project copy operation is active.
 ********************************************************************************************************************/
inline bool MainWindow::isProjectCopied() const
{
    return (m_currentCopyState == PROJECT);
}

/*!*******************************************************************************************************************
 * \brief Returns true if a group (cell) copy operation is active.
 ********************************************************************************************************************/
inline bool MainWindow::isGroupCopied() const
{
    return (m_currentCopyState == GROUP);
}

/*!*******************************************************************************************************************
 * \brief Returns true if a view copy operation is active.
 ********************************************************************************************************************/
inline bool MainWindow::isViewCopied() const
{
    return (m_currentCopyState == VIEW);
}

/*!*******************************************************************************************************************
 * \brief Returns the application window title (DOOR).
 ********************************************************************************************************************/
inline QString MainWindow::getAppTitle() const
{
    return QString("DOOR - Library Manager for Open EDA");
}

#endif // MAINWINDOW_H
