#include <QMenu>
#include <QFile>
#include <QDebug>
#include <QScreen>
#include <QProcess>
#include <QVariant>
#include <QFileInfo>
#include <QSettings>
#include <QMessageBox>
#include <QMouseEvent>
#include <QTextStream>
#include <QFileDialog>
#include <QDirIterator>
#include <QInputDialog>
#include <QTemporaryFile>
#include <QGuiApplication>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QDesktopServices>
#include <QJsonObject>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QFont>
#include <QKeyEvent>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QSplitter>

#include <QListWidgetItem>
#include <QVBoxLayout>

#include "snapshotview.h"
#include "python_console.h"

#if QT_VERSION >= 0x050000
#include <QScreen>
#endif

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "keybindings.h"
#include "keybindingsdialog.h"
#include "libman_test_mode.h"

namespace {

QTextCharFormat logPlainCharFormat()
{
    QTextCharFormat fmt;
    fmt.setForeground(QBrush(Qt::black));
    fmt.setFontUnderline(false);
    fmt.setAnchor(false);
    fmt.setAnchorHref(QString());
    return fmt;
}

QTextCursor logCursorAtEnd(QTextEdit *textEdit, bool clear)
{
    if(clear) {
        textEdit->clear();
    }

    QTextCursor cursor = textEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.setCharFormat(logPlainCharFormat());
    return cursor;
}

QString normalizedWatchPath(const QString &path)
{
    if(path.isEmpty()) {
        return QString();
    }
    return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}

bool watchPathsEqual(const QString &left, const QString &right)
{
    const QString a = normalizedWatchPath(left);
    const QString b = normalizedWatchPath(right);
    if(a.isEmpty() || b.isEmpty()) {
        return false;
    }
#if defined(Q_OS_WIN)
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

} // namespace

#include "about.h"
#include "newview.h"
#include "property.h"
#include "view_tools.h"
#include "toolmanager.h"
#include "libfileparser.h"
#include "projectmanager.h"
#include "projecteditor.h"
#include "importdialog.h"
#include "exportdialog.h"
#include "door_pdk_descriptor_dialog.h"
#include "room/room_path_utils.h"
#include "room/room_file_lock.h"
#include "assistant/assistantchatpanel.h"
#include "assistant/assistantmcp.h"
#include "assistant/assistantagent.h"
#include "assistant/roommcpclient.h"
#include "assistant/securestore.h"

#ifndef LIBMAN_NO_ROOM
#  include "database.h"
#  include "room_json_io.h"
#endif

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>
#include <QVBoxLayout>

/*!*******************************************************************************************************************
 * \brief Constructs a LibMan MainWindow object with the given arguments.
 * \param projFile      Path to the project file. Be default, it will be searched in the current folder.
 * \param runDir        Path to the directory, where application was executed.
 * \param parent        Parent widget, by default is NULL.
 *********************************************************************************************************************/
MainWindow::MainWindow(const QString &projFile, const QString &runDir, QWidget *parent) :
    QMainWindow(parent),
    m_ui(new Ui::MainWindow),
    m_properties(new Properties),
    m_isStateChanged(false),
    m_itemText(""),
    m_runDirectory(runDir),
    m_currentProjFile(QString("")),
    m_currentCopyState(NONE),
    m_projFileWatcher(new QFileSystemWatcher(this)),
    m_coreLockWatcher(new QFileSystemWatcher(this))
{
    m_ui->setupUi(this);

    m_ui->actionProjects->setVisible(false);
    m_ui->textMessages->setReadOnly(true);
    m_ui->textMessages->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    m_ui->textMessages->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_ui->textMessages, &QTextEdit::customContextMenuRequested, this, &MainWindow::showMessagesMenu);
    m_ui->textMessages->viewport()->setMouseTracking(true);
    m_ui->textMessages->viewport()->setAttribute(Qt::WA_Hover, true);
    m_ui->textMessages->viewport()->installEventFilter(this);

    connect(&m_klayoutTools, &KLayoutTools::info, this, [this](const QString &msg) { info(msg, false); });
    connect(&m_klayoutTools, &KLayoutTools::error, this, [this](const QString &msg) { error(msg, false); });
    connect(&m_klayoutTools, &KLayoutTools::fileLink, this, &MainWindow::appendLogFileLink);

    setupPythonConsole();
    setupAssistant();

    m_snapshotView = new SnapshotView(m_ui->snapshotHost);
    auto *snapshotLayout = new QVBoxLayout(m_ui->snapshotHost);
    snapshotLayout->setContentsMargins(0, 0, 0, 0);
    snapshotLayout->addWidget(m_snapshotView);

    m_ui->groupCats->setVisible(false);
    m_ui->groupDocs->setVisible(false);
    m_ui->groupSnapshot->setVisible(false);
    m_ui->actionShow_Documents->setChecked(false);
    m_ui->actionShow_Categories->setChecked(false);
    m_ui->actionShow_Snapshot->setChecked(false);
    m_ui->splitterView->setStretchFactor(0, 2);
    m_ui->splitterView->setStretchFactor(1, 1);

    m_ui->actionGroup->setEnabled(false);
    m_ui->actionUnion->setEnabled(false);
    m_ui->actionCategory->setEnabled(false);

    initRecentProjectMenu();

    loadSettings();
    applyKeyBindings();
    warmWslForXschemIfNeeded();

    setWindowIcon(QIcon(":appicon"));
    initIcons();

    m_ui->listGroups->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_ui->listViews->setSelectionMode(QAbstractItemView::ExtendedSelection);

    m_ui->treeLibs->setContextMenuPolicy(Qt::CustomContextMenu);
    m_ui->listViews->setContextMenuPolicy(Qt::CustomContextMenu);
    m_ui->listGroups->setContextMenuPolicy(Qt::CustomContextMenu);
    m_ui->listCategories->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_ui->treeLibs, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showLibraryMenu(const QPoint &)));
    connect(m_ui->listViews, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showViewMenu(const QPoint &)));
    connect(m_ui->listGroups, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showGroupMenu(const QPoint &)));
    connect(m_ui->listCategories, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showCategoryMenu(const QPoint &)));
    connect(m_ui->listViews, SIGNAL(itemExpanded(QTreeWidgetItem*)), this, SLOT(on_viewItemExpanded(QTreeWidgetItem*)));
    connect(m_ui->listViews, &QTreeWidget::itemSelectionChanged, this, &MainWindow::refreshSnapshot);
    connect(m_ui->treeLibs, SIGNAL(itemSelectionChanged()), this, SLOT(on_treeLibs_itemSelectionChanged()));

    m_ui->listViews->viewport()->installEventFilter(this);

    setupDragAndDrop();

    setWindowTitle(getAppTitle());

    if(QFileInfo(projFile).exists()) {
        loadProjectFile(projFile);
    }
    else {
        QString localProjFile = getProjectFileFromDir(runDir);
        if(QFileInfo(localProjFile).exists()) {
            loadProjectFile(localProjFile);
        }
    }

    connect(m_projFileWatcher,
            SIGNAL(fileChanged(QString)),
            this,
            SLOT(onProjectFileChanged(QString)));

    connect(m_coreLockWatcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::onCoreLockWatcherChanged);
    connect(m_coreLockWatcher, &QFileSystemWatcher::directoryChanged,
            this, &MainWindow::onCoreLockWatcherChanged);

    m_coreLockRefreshTimer = new QTimer(this);
    m_coreLockRefreshTimer->setSingleShot(true);
    m_coreLockRefreshTimer->setInterval(300);
    connect(m_coreLockRefreshTimer, &QTimer::timeout, this, [this]() {
        refreshCoreViewLockItems();
        syncCoreLockWatches();
        maybeReloadCurrentCellViews();
    });

    m_coreLockPollTimer = new QTimer(this);
    m_coreLockPollTimer->setInterval(2000);
    connect(m_coreLockPollTimer, &QTimer::timeout, this, [this]() {
        if(m_coreLockWatchedFiles.isEmpty()) {
            return;
        }
        refreshCoreViewLockItems();
        syncCoreLockWatches();
    });

    m_ui->listViews->setHeaderHidden(true);
}

/*!*******************************************************************************************************************
 * \brief Destroys the main window.
 *********************************************************************************************************************/
MainWindow::~MainWindow()
{
    delete m_ui;
    delete m_properties;
}

/*!********************************************************************************************************************
 * \brief Initializes all action icons in the main window.
 *
 * This function assigns SVG icons to all UI actions, ensuring a consistent
 * visual style across menus and toolbars. It should be called once after
 * the UI has been set up (e.g. in the constructor after setupUi()).
 *********************************************************************************************************************/
void MainWindow::initIcons()
{
    // File / Project
    m_ui->actionProject->setIcon(QIcon(":/icons/category.svg"));
    m_ui->actionGroup->setIcon(QIcon(":/icons/category.svg"));
    m_ui->actionUnion->setIcon(QIcon(":/icons/view.svg"));

    // Open / Save
    m_ui->actionOpen->setIcon(QIcon(":/icons/category.svg"));
    m_ui->actionSave->setIcon(QIcon(":/icons/save.svg"));
    m_ui->actionSave_As->setIcon(QIcon(":/icons/save.svg"));

    // Exit
    m_ui->actionExit->setIcon(QIcon(":/icons/exit.svg"));

    // View switching
    m_ui->actionShow_Documents->setIcon(QIcon(":/icons/show_documents.svg"));
    m_ui->actionShow_Categories->setIcon(QIcon(":/icons/show_categories.svg"));
    m_ui->actionShow_Snapshot->setIcon(QIcon(":/icons/show_snapshot.svg"));

    // Category
    m_ui->actionCategory->setIcon(QIcon(":/icons/category.svg"));

    // Session
    m_ui->actionSession->setIcon(QIcon(":/icons/view.svg"));

    // Reload
    m_ui->actionReload->setIcon(QIcon(":/icons/reload.svg"));

    // Tools / About
    m_ui->actionTools->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    m_ui->actionAbout->setIcon(QIcon(":/icons/info.svg"));

    // Recent
    QIcon recentIcon(":/icons/documents.svg");
    m_ui->actionRecent1->setIcon(recentIcon);
    m_ui->actionRecent2->setIcon(recentIcon);
    m_ui->actionRecent3->setIcon(recentIcon);
    m_ui->actionRecent4->setIcon(recentIcon);
    m_ui->actionRecent5->setIcon(recentIcon);

    // Clear recent
    m_ui->actionClear_Recent_File_Stack->setIcon(QIcon(":/icons/delete.svg"));
}

/*!*******************************************************************************************************************
 * \brief This event handler is called with the given event when Qt receives a window close request for a top-level
 * widget from the window system. The function is used to save the projects settings.
 * \param event       Variable is used to either accept to close the window or ignore to prevent closing it.
 **********************************************************************************************************************/
void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings(getSettingsHeaderName());
    settings.setDefaultFormat(QSettings::NativeFormat);

    settings.beginGroup("Appearance");
    settings.setValue("Geometry", saveGeometry());
    settings.setValue("WindowState", saveState());
    settings.setValue("ShowDocuments", m_ui->groupDocs->isVisible());
    settings.setValue("ShowCategories", m_ui->groupCats->isVisible());
    settings.setValue("ShowSnapshot", m_ui->groupSnapshot->isVisible());
    settings.setValue("ShowMessages", m_ui->actionShow_Messages->isChecked());
    settings.setValue("ShowCommandLine", m_ui->actionShow_Command_Line->isChecked());
    settings.setValue("ShowAssistant", m_ui->actionShow_Assistant->isChecked());
    settings.setValue("SnapshotSplitter", m_ui->splitterView->saveState());
    if (m_ui->splitterMessages) {
        settings.setValue("MessagesSplitter", m_ui->splitterMessages->saveState());
    }
    if (m_ui->splitterBottom) {
        settings.setValue("BottomSplitter", m_ui->splitterBottom->saveState());
    }
    settings.endGroup();

    if (m_pythonConsole) {
        m_pythonConsole->stop();
    }

    settings.beginGroup("Tools");

    QString schematic = "nedit";
    if(m_properties->exists("Schematic")) {
        schematic = m_properties->get<QString>("Schematic");
    }

    QString layout = "klayout";
    if(m_properties->exists("Layout")) {
        layout = m_properties->get<QString>("Layout");
    }

    QString editor = "nedit";
    if(m_properties->exists("Editor")) {
        editor = m_properties->get<QString>("Editor");
    }

    QString pdfReader = "";
    if(m_properties->exists("PdfReader")) {
        pdfReader = m_properties->get<QString>("PdfReader");
    }

    if(m_properties->exists("ToolList")) {
        QStringList tools = m_properties->get<QString>("ToolList").split(",");
        settings.setValue("ToolList", tools);
        foreach(const QString &name, tools) {
            if(m_properties->exists(name)) {
                settings.setValue(name, m_properties->get<QString>(name));
            }

            if(m_properties->exists(name + "Views")) {
                settings.setValue(name + "Views", m_properties->get<QString>(name + "Views"));
            }

            const QString toolsKey = viewToolsPropertyKey(name);
            if(m_properties->exists(toolsKey)) {
                settings.setValue(toolsKey, m_properties->get<QString>(toolsKey));
            }
        }
    }

    settings.setValue("Editor", editor);
    settings.setValue("PdfReader", pdfReader);

    QString schematicSymbolFormat = QStringLiteral("ROOM");
    if (m_properties->exists(QStringLiteral("SchematicSymbolFormat"))) {
        schematicSymbolFormat =
            m_properties->get<QString>(QStringLiteral("SchematicSymbolFormat")).trimmed().toUpper();
    }
    settings.setValue(QStringLiteral("SchematicSymbolFormat"), schematicSymbolFormat);

    // Assistant LLM: BASE_URL/MODEL cleartext; API key only as DPAPI blob.
    persistAssistantSettings();
    settings.endGroup();

    checkAndSaveProjectData(event);

    QMainWindow::closeEvent(event);
}

/*!*******************************************************************************************************************
 * \brief If state of the LibMan has been changed the dialog window pops up to ask user for saving of the changes.
 * \param event       Variable is used to either accept to close the window or ignore to prevent closing it.
 **********************************************************************************************************************/
void MainWindow::checkAndSaveProjectData(QCloseEvent *event)
{
    QMessageBox msgBox;
    msgBox.setText("The project settings have been modified.");
    msgBox.setInformativeText("Do you want to save your changes?");
    msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);

    QString windowTitle = this->windowTitle();
    if(windowTitle.contains("*")) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QScreen* pScreen = QGuiApplication::screenAt(this->mapToGlobal(QPoint(this->width() / 2, 0)));
        QRect screenRect = pScreen ? pScreen->availableGeometry() : QRect();
#elif QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        QScreen* pScreen = QGuiApplication::screenAt(this->mapToGlobal(QPoint(this->width() / 2, 0)));
        QRect screenRect = pScreen ? pScreen->availableGeometry() : QRect();
#else
        QRect screenRect = QApplication::desktop()->screenGeometry(this);
#endif

        msgBox.move(QPoint(screenRect.width()/2, screenRect.height()/2));

        int ret = msgBox.exec();

        switch (ret) {
            case QMessageBox::Save:
                on_actionSave_triggered();
                event->accept();
                break;
            case QMessageBox::Discard:
                event->accept();
                break;
            case QMessageBox::Cancel:
                event->ignore();
                break;
            default:
                break;
        }
    }
}

/*!*******************************************************************************************************************
 * \brief Loads LibMan settings (geometry, tools, widget states etc.) and aplies them accordingly.
 **********************************************************************************************************************/
void MainWindow::loadSettings()
{
    QSettings settings(getSettingsHeaderName());

    settings.beginGroup("Appearance");
    restoreGeometry(settings.value("Geometry").toByteArray());
    restoreState(settings.value("WindowState").toByteArray());

    m_ui->groupDocs->setVisible(settings.value("ShowDocuments", false).toBool());
    m_ui->groupCats->setVisible(settings.value("ShowCategories", false).toBool());
    m_ui->groupSnapshot->setVisible(settings.value("ShowSnapshot", false).toBool());
    m_ui->actionShow_Documents->setChecked(settings.value("ShowDocuments", false).toBool());
    m_ui->actionShow_Categories->setChecked(settings.value("ShowCategories", false).toBool());
    m_ui->actionShow_Snapshot->setChecked(settings.value("ShowSnapshot", false).toBool());
    m_ui->actionShow_Messages->setChecked(settings.value("ShowMessages", true).toBool());
    m_ui->actionShow_Command_Line->setChecked(settings.value("ShowCommandLine", true).toBool());
    m_ui->actionShow_Assistant->setChecked(settings.value("ShowAssistant", true).toBool());
    m_ui->splitterView->restoreState(settings.value("SnapshotSplitter").toByteArray());
    if (m_ui->splitterMessages) {
        const QByteArray messagesState = settings.value("MessagesSplitter").toByteArray();
        if (!messagesState.isEmpty()) {
            m_ui->splitterMessages->restoreState(messagesState);
        } else {
            m_ui->splitterMessages->setSizes({300, 80});
        }
    }
    if (m_ui->splitterBottom) {
        const QByteArray bottomState = settings.value("BottomSplitter").toByteArray();
        if (!bottomState.isEmpty()) {
            m_ui->splitterBottom->restoreState(bottomState);
        } else {
            m_ui->splitterBottom->setSizes({520, 360});
        }
    }
    updateMessagesPanelVisibility();

    settings.endGroup();

    settings.beginGroup("Tools");

    QString editor = "nedit";
    if(settings.contains("Editor")) {
        editor = settings.value("Editor").toString();
    }
    m_properties->set("Editor", editor);

    QString pdfReader = "";
    if(settings.contains("PdfReader")) {
        pdfReader = settings.value("PdfReader").toString();
    }

    if(settings.contains("ToolList")) {
        QStringList tools = settings.value("ToolList").toStringList();
        if(tools.count()) {
            m_properties->set("ToolList", tools.join(","));
            foreach(const QString name, tools) {
                if(settings.contains(name + "Views")) {
                    const QString views = settings.value(name + "Views").toString();
                    m_properties->set(name + "Views", views);
                }
                if(settings.contains(name)) {
                    m_properties->set(name, settings.value(name).toString());
                }

                const QString toolsKey = viewToolsPropertyKey(name);
                if(settings.contains(toolsKey)) {
                    m_properties->set(toolsKey, settings.value(toolsKey).toString());
                }
            }
        }
    }
    else {
        m_properties->set("ToolList", "Schematic,Layout");
        m_properties->set("Schematic", "nedit");
        m_properties->set("SchematicViews", "cdl,spice,schematic,symbol");
        m_properties->set("Layout", "klayout");
        m_properties->set("LayoutViews", "gds,oas,lstr,layout");
    }

    m_properties->set("PdfReader", pdfReader);

    QString schematicSymbolFormat = QStringLiteral("ROOM");
    if (settings.contains(QStringLiteral("SchematicSymbolFormat"))) {
        schematicSymbolFormat =
            settings.value(QStringLiteral("SchematicSymbolFormat")).toString().trimmed().toUpper();
    }
    if (schematicSymbolFormat != QStringLiteral("JSON")) {
        schematicSymbolFormat = QStringLiteral("ROOM");
    }
    m_properties->set(QStringLiteral("SchematicSymbolFormat"), schematicSymbolFormat);

    m_properties->set(
        QStringLiteral("ASSISTANT_BASE_URL"),
        settings.value(QStringLiteral("ASSISTANT_BASE_URL"),
                       QStringLiteral("https://api.openai.com/v1")).toString());
    m_properties->set(
        QStringLiteral("ASSISTANT_MODEL"),
        settings.value(QStringLiteral("ASSISTANT_MODEL"),
                       QStringLiteral("gpt-4o-mini")).toString());
    {
        const QString enc = settings.value(QStringLiteral("ASSISTANT_API_KEY_DPAPI")).toString();
        const QString plain = SecureStore::unprotectFromBase64(enc);
        m_properties->set(QStringLiteral("ASSISTANT_API_KEY"), plain);
    }

    settings.endGroup();

    configureAssistantAgent();
}

/*!*******************************************************************************************************************
 * \brief Starts the WSL2 VM in the background when the schematic tool is open-xschem-wsl.
 **********************************************************************************************************************/
void MainWindow::warmWslForXschemIfNeeded()
{
#ifdef Q_OS_WIN
    auto looksLikeXschemWsl = [](const QString &path) -> QString {
        const QString trimmed = path.trimmed();
        if(trimmed.isEmpty()) {
            return {};
        }
        const QFileInfo fi(trimmed);
        if(fi.fileName().compare(QStringLiteral("open-xschem-wsl.bat"), Qt::CaseInsensitive) == 0
           || fi.fileName().compare(QStringLiteral("open-xschem-wsl.sh"), Qt::CaseInsensitive) == 0) {
            return fi.absoluteFilePath();
        }
        const QString batInDir = QDir(trimmed).filePath(QStringLiteral("open-xschem-wsl.bat"));
        if(QFileInfo::exists(batInDir)) {
            return batInDir;
        }
        return {};
    };

    // Scan configured tool paths only — never call resolveViewToolPath() here,
    // it pops the Choose Tool dialog when Xschem and Qucs-S are both present.
    QStringList candidates;
    const QStringList groups = m_properties->get<QString>(QStringLiteral("ToolList"))
                                   .split(QLatin1Char(','), Qt::SkipEmptyParts);
    for(const QString &groupName : groups) {
        const QString group = groupName.trimmed();
        if(group.isEmpty()) {
            continue;
        }
        if(m_properties->exists(group)) {
            candidates << m_properties->get<QString>(group);
        }
        for(const ViewToolEntry &entry : loadViewTools(m_properties, group)) {
            candidates << entry.path;
        }
    }
    if(m_properties->exists(QStringLiteral("Schematic"))) {
        candidates << m_properties->get<QString>(QStringLiteral("Schematic"));
    }
    if(m_properties->exists(QStringLiteral("Editor"))) {
        candidates << m_properties->get<QString>(QStringLiteral("Editor"));
    }

    QString wslBat;
    for(const QString &candidate : candidates) {
        wslBat = looksLikeXschemWsl(candidate);
        if(!wslBat.isEmpty()) {
            break;
        }
    }

    if(wslBat.isEmpty()) {
        return;
    }

    const QString workDir = QFileInfo(wslBat).absolutePath();
    const QString wslScript = QDir(workDir).filePath(QStringLiteral("open-xschem-wsl.sh"));
    if(!QFileInfo::exists(wslScript)) {
        QProcess::startDetached(QStringLiteral("C:\\Windows\\System32\\wsl.exe"),
                                QStringList() << QStringLiteral("-e") << QStringLiteral("true"));
        return;
    }

    auto toWslPath = [](const QString &winPath) -> QString {
        const QString abs = QDir::fromNativeSeparators(QFileInfo(winPath).absoluteFilePath());
        if(abs.size() >= 2 && abs.at(1) == QLatin1Char(':')) {
            return QStringLiteral("/mnt/") + abs.at(0).toLower() + abs.mid(2);
        }
        return abs;
    };

    QProcess::startDetached(QStringLiteral("C:\\Windows\\System32\\wsl.exe"),
                            QStringList()
                                << QStringLiteral("--cd")
                                << toWslPath(workDir)
                                << QStringLiteral("bash")
                                << QStringLiteral("-lc")
                                << QStringLiteral("./open-xschem-wsl.sh --warm"));
#endif
}

/*!*******************************************************************************************************************
 * \brief Returns list of the currently loaded projects (libraries).
 **********************************************************************************************************************/
QMap<QString, QString> MainWindow::getCurrentLibraries() const
{
    QMap<QString, QString> libMap;

    const QMap<QString, PropertyItem*> propItems = m_properties->getMap();

    for(auto it = propItems.constBegin(); it != propItems.constEnd(); ++it) {
        const QString key = it.key();
        if(!key.toUpper().startsWith(getLibraryKeyPrefix())) {
            continue;
        }

        QString tail = key;
        tail.remove(getLibraryKeyPrefix());

        const int pos = tail.indexOf('/');
        const QString libName = (pos >= 0) ? tail.left(pos).trimmed() : tail.trimmed();
        if(libName.isEmpty()) {
            continue;
        }

        const QString filePath = m_properties->get<QString>(key).trimmed();
        QFileInfo fi(filePath);
        if(!fi.exists()) {
            continue;
        }

        if (fi.isDir()) {
            if (!libMap.contains(libName)) {
                libMap[libName] = QDir::toNativeSeparators(fi.absoluteFilePath());
            }
            continue;
        }

        if(!fi.isFile()) {
            continue;
        }

        QDir libDir = fi.dir();
        if(!libDir.cdUp()) {
            continue;
        }

        const QString rootPath = QDir::toNativeSeparators(libDir.absolutePath());

        if(!libMap.contains(libName)) {
            libMap[libName] = rootPath;
        }
    }

    return libMap;
}

/*!*******************************************************************************************************************
 * \brief Triggers execution of ToolManager dialog window.
 **********************************************************************************************************************/
void MainWindow::on_actionTools_triggered()
{
    ToolManager(this, m_properties).exec();
    persistAssistantSettings();
    configureAssistantAgent();
}

/*!*******************************************************************************************************************
 * \brief Triggers execution of ProjectManager dialog window.
 *********************************************************************************************************************/
void MainWindow::on_actionProjects_triggered()
{
    ProjectManager(this, m_properties).exec();
}

void MainWindow::on_actionEditProject_triggered()
{
    if (!m_projectEditor) {
        m_projectEditor = new ProjectEditor(this);
        m_projectEditor->setMinimumWidth(420);
        m_ui->splitter_2->addWidget(m_projectEditor);
        m_projectEditor->hide();
    }

    if (m_projectEditor->isVisible()) {
        if (!m_projectEditor->confirmHide()) {
            return;
        }
        m_projectEditor->hide();
        return;
    }

    m_projectEditor->reloadFromDisk();
    m_projectEditor->show();

    QList<int> sizes = m_ui->splitter_2->sizes();
    const int index = m_ui->splitter_2->indexOf(m_projectEditor);
    if (index < 0 || index >= sizes.size()) {
        return;
    }

    const int wanted = 520;
    if (sizes.at(index) >= wanted) {
        return;
    }

    int deficit = wanted - sizes.at(index);
    sizes[index] = wanted;
    for (int i = 0; i < sizes.size() && deficit > 0; ++i) {
        if (i == index) {
            continue;
        }
        const int spare = qMax(0, sizes.at(i) - 140);
        const int take = qMin(deficit, spare);
        sizes[i] -= take;
        deficit -= take;
    }
    m_ui->splitter_2->setSizes(sizes);
}

void MainWindow::on_actionImport_triggered()
{
#ifndef LIBMAN_NO_ROOM
    ImportDialog dialog(this);
    dialog.exec();
#else
    error(tr("Import requires ROOM support. Rebuild DOOR without CONFIG+=no_room."), false);
#endif
}

void MainWindow::on_actionExport_triggered()
{
#ifndef LIBMAN_NO_ROOM
    ExportDialog dialog(this);
    dialog.exec();
#else
    error(tr("Export requires ROOM support. Rebuild DOOR without CONFIG+=no_room."), false);
#endif
}

/*!*******************************************************************************************************************
 * \brief Initializes recent menu and assigns menu items with assotiated project files.
 ********************************************************************************************************************/
void MainWindow::initRecentProjectMenu()
{
    m_recentProjects.push_back(m_ui->actionRecent1);
    m_recentProjects.push_back(m_ui->actionRecent2);
    m_recentProjects.push_back(m_ui->actionRecent3);
    m_recentProjects.push_back(m_ui->actionRecent4);
    m_recentProjects.push_back(m_ui->actionRecent5);

    for(int i = 0; i < m_recentProjects.count(); ++i) {
        m_recentProjects[i]->setVisible(false);
        connect(m_recentProjects[i],
                SIGNAL(triggered()),
                this,
                SLOT(loadRecentProject()));
    }

    updateRecentProjectActions();
}

/*!*******************************************************************************************************************
 * \brief Loads the most recent project file into LibMan environment.
 **********************************************************************************************************************/
void MainWindow::loadRecentProject()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (action) {
        QString projFile = action->data().toString();
        if(!QFileInfo(projFile).exists()) {
            QMessageBox::critical(this,
                                  "Open Project Error",
                                  QString("Can not open project '%1'.").arg(projFile));
            return;
        }

        loadProjectFile(projFile);
    }
}

/*!*******************************************************************************************************************
 * \brief Opens the top entry of the Recent Projects list (shortcut: Open most recent).
 **********************************************************************************************************************/
void MainWindow::openMostRecentProject()
{
    if (!m_ui->actionRecent1 || !m_ui->actionRecent1->isVisible()) {
        QMessageBox::information(this,
                                 tr("Open Most Recent Project"),
                                 tr("No recent project is available."));
        return;
    }
    const QString projFile = m_ui->actionRecent1->data().toString();
    if (projFile.isEmpty() || !QFileInfo(projFile).exists()) {
        QMessageBox::critical(this,
                              tr("Open Project Error"),
                              tr("Can not open project '%1'.").arg(projFile));
        return;
    }
    loadProjectFile(projFile);
}

/*!*******************************************************************************************************************
 * \brief Applies persisted (or default) shortcuts to the matching menu actions.
 **********************************************************************************************************************/
void MainWindow::applyKeyBindings()
{
    QSettings settings(getSettingsHeaderName());
    const QMap<QString, QKeySequence> map = KeyBindings::load(settings);

    auto setShortcut = [&map](QAction *action, const QString &id) {
        if (!action) {
            return;
        }
        action->setShortcut(map.value(id));
    };

    setShortcut(m_ui->actionOpen, QStringLiteral("open_project"));
    setShortcut(m_ui->actionOpenMostRecent, QStringLiteral("open_most_recent"));
    setShortcut(m_ui->actionSave, QStringLiteral("save_project"));
    setShortcut(m_ui->actionSave_As, QStringLiteral("save_project_as"));
    setShortcut(m_ui->actionEditProject, QStringLiteral("edit_project"));
    setShortcut(m_ui->actionImport, QStringLiteral("import"));
    setShortcut(m_ui->actionExport, QStringLiteral("export"));
    setShortcut(m_ui->actionExit, QStringLiteral("exit"));
    setShortcut(m_ui->actionReload, QStringLiteral("reload"));
    setShortcut(m_ui->actionProjects, QStringLiteral("projects_dialog"));
    setShortcut(m_ui->actionTools, QStringLiteral("tools_dialog"));
    setShortcut(m_ui->actionKeyBindings, QStringLiteral("key_bindings"));
}

/*!*******************************************************************************************************************
 * \brief Opens the Key Bindings dialog and saves accepted shortcuts.
 **********************************************************************************************************************/
void MainWindow::on_actionKeyBindings_triggered()
{
    QSettings settings(getSettingsHeaderName());
    KeyBindingsDialog dlg(KeyBindings::load(settings), this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    KeyBindings::save(settings, dlg.bindings());
    applyKeyBindings();
}

/*!*******************************************************************************************************************
 * \brief Menu / shortcut entry for opening the most recent project.
 **********************************************************************************************************************/
void MainWindow::on_actionOpenMostRecent_triggered()
{
    openMostRecentProject();
}

/*!*******************************************************************************************************************
 * \brief Sets the name of the last loaded project to the upper recent file list.
 * \param fileName     Name of the project file.
 **********************************************************************************************************************/
void MainWindow::setRecentProject(const QString &fileName)
{
    if(fileName.isEmpty()) {
        return;
    }

    const QString absFileName = QFileInfo(fileName).absoluteFilePath();

    QSettings settings(getSettingsHeaderName());
    settings.beginGroup("RecentProjects");

    QStringList files = settings.value("RecentProjList").toStringList();
    for(int i = files.size() - 1; i >= 0; --i) {
        if(QFileInfo(files[i]).absoluteFilePath() == absFileName) {
            files.removeAt(i);
        }
    }
    files.prepend(absFileName);
    while (files.size() > PROJ_MAX_COUNT) {
        files.removeLast();
    }

    settings.setValue("RecentProjList", files);
    settings.endGroup();

    updateRecentProjectActions();
}

/*!*******************************************************************************************************************
 * \brief Updates File->Recent menu items.
 **********************************************************************************************************************/
void MainWindow::updateRecentProjectActions()
{
    QSettings settings(getSettingsHeaderName());
    settings.beginGroup("RecentProjects");
    QStringList files = settings.value("RecentProjList").toStringList();
    settings.endGroup();

    int slot = 0;
    for(int i = 0; i < files.size() && slot < PROJ_MAX_COUNT; ++i) {
        const QString absFileName = QFileInfo(files[i]).absoluteFilePath();
        if(!QFileInfo(absFileName).exists()) {
            continue;
        }

        const QString text = tr("&%1 %2").arg(slot + 1).arg(absFileName);
        m_recentProjects[slot]->setText(text);
        m_recentProjects[slot]->setData(absFileName);
        m_recentProjects[slot]->setVisible(true);
        ++slot;
    }

    for(int j = slot; j < PROJ_MAX_COUNT; ++j) {
        m_recentProjects[j]->setVisible(false);
    }
}

/*!*******************************************************************************************************************
 * \brief Returns current working directory from where the project file has been loaded, otherwise user home path.
 **********************************************************************************************************************/
QString MainWindow::getCurrentWorkingDir() const
{
    if(!m_currentProjFile.isEmpty()) {
        const QString projDir = QFileInfo(m_currentProjFile).absolutePath();
        if(QFileInfo(projDir).exists()) {
            return projDir;
        }
    }

    if(m_ui->actionRecent1 && m_ui->actionRecent1->isVisible()) {
        const QString recentFile = m_ui->actionRecent1->data().toString();
        if(!recentFile.isEmpty()) {
            const QString workDir = QFileInfo(recentFile).absolutePath();
            if(QFileInfo(workDir).exists()) {
                return workDir;
            }
        }
    }

    return m_runDirectory;
}

/*!*******************************************************************************************************************
 * \brief Returns library path based on the project (library) name.
 * \param libName      Name of the library.
 **********************************************************************************************************************/
QString MainWindow::getLibraryPath(const QString &libName) const
{
    if(libName.isEmpty()) {
        return QString();
    }

    const QString rootKey = getLibraryKeyPrefix() + libName;
    if(m_properties && m_properties->exists(rootKey)) {
        const QString rootPath = m_properties->get<QString>(rootKey).trimmed();
        QFileInfo rootInfo(rootPath);
        if(rootInfo.exists() && rootInfo.isDir()) {
            return QDir::toNativeSeparators(rootInfo.absoluteFilePath());
        }
    }

    const QMap<QString, PropertyItem*> propItems = m_properties->getMap();
    const QString prefix = getLibraryKeyPrefix() + libName + "/";

    for(auto it = propItems.constBegin(); it != propItems.constEnd(); ++it) {
        const QString key = it.key();
        if(!key.startsWith(prefix)) {
            continue;
        }

        const QString filePath = m_properties->get<QString>(key).trimmed();
        if(filePath.isEmpty()) {
            continue;
        }

        QFileInfo fi(filePath);
        if(!fi.exists() || !fi.isFile()) {
            continue;
        }

        QDir viewDir = fi.dir();
        if(!viewDir.cdUp()) {
            continue;
        }

        return QDir::toNativeSeparators(viewDir.absolutePath());
    }

    return QString();
}

/*!*******************************************************************************************************************
 * \brief Stores the root directory for a library created without layout files.
 **********************************************************************************************************************/
void MainWindow::setLibraryRootDirectory(const QString &libName, const QString &dirPath)
{
    if(libName.isEmpty() || dirPath.isEmpty()) {
        return;
    }

    const QString key = getLibraryKeyPrefix() + libName;
    m_properties->set(key, QDir::toNativeSeparators(dirPath));
}

/*!*******************************************************************************************************************
 * \brief Removes all project properties for a library (root path and cell views).
 ********************************************************************************************************************/
void MainWindow::removeLibraryPropertyKeys(const QString &libName)
{
    if(libName.isEmpty()) {
        return;
    }

    const QString rootKey = getLibraryKeyPrefix() + libName;
    const QString prefix = rootKey + "/";
    QStringList keysToRemove;

    const QMap<QString, PropertyItem*> propItems = m_properties->getMap();
    for(auto it = propItems.constBegin(); it != propItems.constEnd(); ++it) {
        const QString key = it.key();
        if(key == rootKey || key.startsWith(prefix)) {
            keysToRemove << key;
        }
    }

    for(const QString &key : keysToRemove) {
        m_properties->remove(key);
    }

    clearTechLibraryAttach(libName);

    const QList<QPair<QString, QString>> attaches = getTechLibraryAttaches();
    for(const QPair<QString, QString> &attach : attaches) {
        if(attach.second == libName) {
            clearTechLibraryAttach(attach.first);
        }
    }
}

/*!*******************************************************************************************************************
 * \brief Removes all project properties for a cell.
 ********************************************************************************************************************/
void MainWindow::removeCellPropertyKeys(const QString &libName, const QString &groupName)
{
    if(libName.isEmpty() || groupName.isEmpty()) {
        return;
    }

    const QString prefix = getLibraryKeyPrefix() + libName + "/" + groupName + "/";
    QStringList keysToRemove;

    const QMap<QString, PropertyItem*> propItems = m_properties->getMap();
    for(auto it = propItems.constBegin(); it != propItems.constEnd(); ++it) {
        const QString key = it.key();
        if(key.startsWith(prefix)) {
            keysToRemove << key;
        }
    }

    for(const QString &key : keysToRemove) {
        m_properties->remove(key);
    }
}

/*!*******************************************************************************************************************
 * \brief Launches a dialog window to select a lib file to load.
 **********************************************************************************************************************/
void MainWindow::on_actionOpen_triggered()
{
    QString workDir = getCurrentWorkingDir();
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    tr("Open Project..."),
                                                    workDir,
                                                    tr("DOOR project (*.projects *.lib);;All files (*)"));

    if(!fileName.isEmpty()) {
        loadProjectFile(fileName);
    }
}

/*!*******************************************************************************************************************
 * \brief Displays basic message in the output text widget of LibMan.
 * \param msg         Message to output.
 * \param clear       Flag to clear the output text widget of LibMan before displaying the message.
 **********************************************************************************************************************/
void MainWindow::info(const QString &msg, bool clear)
{
    QTextCursor cursor = logCursorAtEnd(m_ui->textMessages, clear);
    const QTextCharFormat fmt = logPlainCharFormat();
    cursor.insertText(QStringLiteral("[INFO] ") + msg + QLatin1Char('\n'), fmt);
    m_ui->textMessages->setTextCursor(cursor);
}

/*!*******************************************************************************************************************
 * \brief Displays error message in the output text widget of LibMan.
 * \param msg         Message to output.
 * \param clear       Flag to clear the output text widget of LibMan before displaying the message.
 **********************************************************************************************************************/
void MainWindow::error(const QString &msg, bool clear)
{
    QTextCursor cursor = logCursorAtEnd(m_ui->textMessages, clear);
    QTextCharFormat fmt = logPlainCharFormat();
    fmt.setForeground(QBrush(Qt::red));
    cursor.insertText(QStringLiteral("[ERROR] ") + msg + QLatin1Char('\n'), fmt);
    cursor.setCharFormat(logPlainCharFormat());
    m_ui->textMessages->setTextCursor(cursor);
}

/*!*******************************************************************************************************************
 * \brief Appends a clickable file link to the message log (opens in KLayout on click).
 **********************************************************************************************************************/
void MainWindow::appendLogFileLink(const QString &filePath, const QString &label)
{
    const QString absPath = QFileInfo(filePath).absoluteFilePath();
    if(absPath.isEmpty() || !QFileInfo::exists(absPath)) {
        return;
    }

    const QString display = label.isEmpty() ? absPath : label;
    const QString href = QUrl::fromLocalFile(absPath).toString(QUrl::FullyEncoded);
    const QString html = QStringLiteral("<a href=\"%1\" style=\"color:#0645ad;text-decoration:underline;\">%2</a>")
                             .arg(href, display.toHtmlEscaped());

    QTextCursor cursor = logCursorAtEnd(m_ui->textMessages, false);
    const QTextCharFormat plainFmt = logPlainCharFormat();
    cursor.insertText(QStringLiteral("[INFO] "), plainFmt);
    cursor.insertHtml(html);
    cursor.insertText(QStringLiteral("\n"), plainFmt);
    cursor.setCharFormat(plainFmt);
    m_ui->textMessages->setTextCursor(cursor);
}

/*!*******************************************************************************************************************
 * \brief Clears the message log widget.
 **********************************************************************************************************************/
void MainWindow::clearMessages()
{
    m_ui->textMessages->clear();
}

/*!*******************************************************************************************************************
 * \brief Context menu for the message log.
 **********************************************************************************************************************/
void MainWindow::showMessagesMenu(const QPoint &pos)
{
    QMenu *menu = m_ui->textMessages->createStandardContextMenu(pos);
    if(!menu) {
        return;
    }

    for(QAction *action : menu->actions()) {
        if(action->text().contains(QStringLiteral("Paste"), Qt::CaseInsensitive)) {
            menu->removeAction(action);
        }
    }

    menu->addSeparator();
    menu->addAction(tr("Clear"), this, &MainWindow::clearMessages);
    menu->exec(m_ui->textMessages->mapToGlobal(pos));
    delete menu;
}

/*!*******************************************************************************************************************
 * \brief Opens a layout file in KLayout when the user clicks a log link.
 **********************************************************************************************************************/
void MainWindow::onLogAnchorClicked(const QUrl &url)
{
    const QString path = url.toLocalFile();
    if(path.isEmpty() || !QFileInfo::exists(path)) {
        error(tr("File not found: %1").arg(url.toString()), false);
        return;
    }

    // New XOR outputs embed master + XOR_DIFF under cell COMPARE (one file).
    const QString baseName = QFileInfo(path).fileName();
    if(baseName.startsWith(QStringLiteral("libman_xor_"), Qt::CaseInsensitive)) {
        info(tr("Opening XOR compare view (master + differences): %1").arg(path), false);
        openLayoutFileInKLayout(path, QStringLiteral("COMPARE"));
        return;
    }

    openLayoutFileInKLayout(path);
}

/*!*******************************************************************************************************************
 * \brief Opens a layout file in KLayout (server or one-shot script).
 **********************************************************************************************************************/
void MainWindow::openLayoutFileInKLayout(const QString &layoutPath, const QString &cellName)
{
    m_klayoutTools.openLayoutFile(getToolByView(QStringLiteral("gds")), layoutPath, cellName, m_currentProjFile);
}

bool MainWindow::isKLayoutServerRunning() const
{
    return m_klayoutTools.isServerRunning();
}

bool MainWindow::ensureKLayoutServerRunning(const QString &tool)
{
    return m_klayoutTools.ensureServerRunning(tool, m_currentProjFile);
}

bool MainWindow::sendKLayoutOpenRequest(const QString &gdsPath, const QString &cellName)
{
    return m_klayoutTools.sendOpenRequest(gdsPath, cellName);
}

bool MainWindow::sendKLayoutSelectRequest(const QString &gdsPath, const QString &cellName)
{
    return m_klayoutTools.sendSelectRequest(gdsPath, cellName);
}

/*!*******************************************************************************************************************
 * \brief Returns list of valid view names.
 **********************************************************************************************************************/
QStringList MainWindow::getValidViewList() const
{
    QStringList views;
    views<<"oas"<<"gds"<<"cdl"<<"spice"<<"verilog";
    return views;
}

/*!*******************************************************************************************************************
 * \brief Returns specified by user tool for displaying views based on view name.
 * \param viewName     Name of the view to return an appropriate tool.
 **********************************************************************************************************************/
QString MainWindow::getToolByView(const QString &viewName) const
{
    const QString view = viewName.trimmed().toLower();
    if(view.isEmpty()) {
        return QString();
    }

    const QStringList groups = m_properties->get<QString>("ToolList").split(",", Qt::SkipEmptyParts);
    for(const QString &groupName : groups) {
        const QString group = groupName.trimmed();
        if(group.isEmpty()) {
            continue;
        }

        if(!layoutViewsForTool(group).contains(view)) {
            continue;
        }

        const QVector<ViewToolEntry> tools = loadViewTools(m_properties, group);
        const QString path = resolveViewToolPath(tools, const_cast<MainWindow*>(this));
        if(!path.isEmpty()) {
            return path;
        }
    }

    return QString();
}

QStringList MainWindow::layoutViewsForTool(const QString &toolName) const
{
    const QString trimmedTool = toolName.trimmed();
    const QString key = trimmedTool + QStringLiteral("Views");
    if(!m_properties->exists(key)) {
        return {};
    }

    QStringList views =
        m_properties->get<QString>(key).remove(QLatin1Char(' ')).split(QLatin1Char(','), Qt::SkipEmptyParts);

    for(QString &view : views) {
        view = view.trimmed().toLower();
    }
    views.removeAll(QString());

    if(trimmedTool == QStringLiteral("Layout") && !views.contains(QStringLiteral("layout"))) {
        views << QStringLiteral("layout");
    }

    views.sort();
    return views;
}

QString MainWindow::layoutPathForKLayout(const QString &viewName,
                                         const QString &viewPath,
                                         QStringList *errors) const
{
    if(isLayoutRoomViewName(viewName)) {
        return roomLayoutPathForKLayout(viewPath, errors);
    }

    return QFileInfo(viewPath).absoluteFilePath();
}

namespace {

LayoutHierarchySnapshot snapshotFromGds(const GdsReader::GdsHierarchy &hierarchy)
{
    LayoutHierarchySnapshot snapshot;
    snapshot.topCells = hierarchy.topCells;
    snapshot.allCells = hierarchy.allCells;
    return snapshot;
}

LayoutHierarchySnapshot snapshotFromOas(const LayoutHierarchy &hierarchy)
{
    LayoutHierarchySnapshot snapshot;
    snapshot.topCells = hierarchy.topCells;
    snapshot.allCells = hierarchy.allCells;
    return snapshot;
}

LayoutHierarchySnapshot snapshotFromCore(const RoomCellReader::CoreHierarchy &hierarchy)
{
    LayoutHierarchySnapshot snapshot;
    snapshot.topCells = hierarchy.topCells;
    snapshot.allCells = hierarchy.allCells;
    return snapshot;
}

LayoutHierarchySnapshot snapshotFromLStream(const QStringList &cellNames)
{
    LayoutHierarchySnapshot snapshot;
    snapshot.topCells = cellNames;
    snapshot.topCells.sort();
    for (const QString &name : cellNames) {
        snapshot.allCells.insert(name);
    }
    return snapshot;
}

} // namespace

QString MainWindow::preferredKLayoutCellForRoot(const QString &layoutPath,
                                              const QString &groupName) const
{
    const QString key = QFileInfo(layoutPath).absoluteFilePath();
    LayoutHierarchySnapshot snapshot;
    bool haveSnapshot = false;

    const auto useSnapshot = [&](const LayoutHierarchySnapshot &candidate) {
        if (!candidate.allCells.isEmpty() || !candidate.topCells.isEmpty()) {
            snapshot = candidate;
            haveSnapshot = true;
        }
    };

    if (const auto it = m_gdsCache.constFind(key); it != m_gdsCache.cend()) {
        const std::shared_ptr<GdsCacheEntry> &entry = it.value();
        if (entry && entry->loaded) {
            useSnapshot(snapshotFromGds(entry->hierarchy));
        }
    }
    if (!haveSnapshot) {
        if (const auto it = m_oasCache.constFind(key); it != m_oasCache.cend()) {
            const std::shared_ptr<OasCacheEntry> &entry = it.value();
            if (entry && entry->loaded) {
                useSnapshot(snapshotFromOas(entry->hierarchy));
            }
        }
    }
    if (!haveSnapshot) {
        if (const auto it = m_coreCache.constFind(key); it != m_coreCache.cend()) {
            const std::shared_ptr<CoreCacheEntry> &entry = it.value();
            if (entry && entry->loaded) {
                useSnapshot(snapshotFromCore(entry->hierarchy));
            }
        }
    }
    if (!haveSnapshot) {
        if (const auto it = m_lstreamCache.constFind(key); it != m_lstreamCache.cend()) {
            const std::shared_ptr<LStreamCacheEntry> &entry = it.value();
            if (entry && entry->loaded) {
                useSnapshot(snapshotFromLStream(entry->cellNames));
            }
        }
    }

    if (!haveSnapshot) {
        QStringList errors;
        haveSnapshot = loadLayoutHierarchySnapshot(layoutPath, snapshot, &errors);
        Q_UNUSED(errors);
    }

    if (!haveSnapshot) {
        return QString();
    }

    return resolveKLayoutRootCell(snapshot, groupName);
}

bool MainWindow::isLayoutViewTreeItem(QTreeWidgetItem *item) const
{
    if (!item) {
        return false;
    }

    const int type = item->data(0, RoleType).toInt();

    if (type == ItemViewGds || type == ItemViewOas || type == ItemViewLStream) {
        return true;
    }

    if (type == ItemViewCore && isLayoutRoomViewName(item->text(0))) {
        return true;
    }

    if (type != ItemCell) {
        return false;
    }

    QTreeWidgetItem *root = item;
    while (root->parent()) {
        root = root->parent();
    }

    const int rootType = root->data(0, RoleType).toInt();
    if (rootType == ItemViewGds || rootType == ItemViewOas || rootType == ItemViewLStream) {
        return true;
    }

    return rootType == ItemViewCore && isLayoutRoomViewName(root->text(0));
}

/*!*******************************************************************************************************************
 * \brief Returns specified by user tool for viewing documents based on doument name.
 * \param documentName     Name of the document to return an appropriate tool.
 **********************************************************************************************************************/
QString MainWindow::getDocumentTool(const QString &documentName) const
{
    QString tool;

    if(QFileInfo(documentName).completeSuffix().toLower() == "pdf") {
        tool = m_properties->get<QString> ("PdfReader");
    }
    else {
        tool = m_properties->get<QString> ("Editor");
    }

    return tool;
}

/*!*******************************************************************************************************************
 * \brief Returns absolute path of the view based on given library/cell/view information.
 *
 * In KLayout .lib based workflow a library points directly to a layout file,
 * therefore the returned path is the library file itself. The groupName and
 * viewName arguments are currently unused and kept only for API compatibility.
 *
 * \param libName       Name of the library.
 * \param groupName     Name of the group (cell).
 * \param viewName      Name of the view.
 **********************************************************************************************************************/
QString MainWindow::getViewPath(const QString &libName,
                                const QString &groupName,
                                const QString &viewName) const
{
    if(libName.isEmpty() || groupName.isEmpty() || viewName.isEmpty()) {
        return QString();
    }

    const QString key = getLibraryKeyPrefix() + libName + "/" + groupName + "/" + viewName;
    if(m_properties->exists(key)) {
        const QString stored = m_properties->get<QString>(key).trimmed();
        if(!stored.isEmpty()) {
            return stored;
        }
    }

    const QString libRoot = getLibraryPath(libName);
    if(libRoot.isEmpty()) {
        return QString();
    }
    const QDir cellDir(QDir(libRoot).filePath(groupName));
    if(!cellDir.exists()) {
        return QString();
    }

    const QString view = viewName.trimmed().toLower();
    if(isEmSetupViewName(view)) {
        const QString emPath = emSetupDirPath(cellDir.absolutePath(), groupName);
        if(QDir(emPath).exists()) {
            return QDir::toNativeSeparators(emPath);
        }
        return QString();
    }
    if(isRoomViewName(view)) {
        const QString roomPath = roomViewFilePath(cellDir.absolutePath(), groupName, view);
        if(QFileInfo::exists(roomPath)) {
            return QDir::toNativeSeparators(roomPath);
        }
        return QString();
    }

    const QString candidate = cellDir.filePath(groupName + QLatin1Char('.') + view);
    if(QFileInfo::exists(candidate)) {
        return QDir::toNativeSeparators(candidate);
    }
    return QString();
}

/*!*******************************************************************************************************************
 * \brief Returns absolute path of the currently selected view file.
 *
 * In KLayout .lib based workflow the library itself points to the layout file,
 * so for layout views such as gds/oas/lstr this function returns the current
 * library file path. For non-layout views an empty string is returned.
 *
 * \param viewName     Name of the view.
 **********************************************************************************************************************/
QString MainWindow::getCurrentViewFilePath(const QString &viewName) const
{
    const QString v = viewName.trimmed().toLower();

    if(v != "gds" && v != "oas" && v != "oasis" && v != "lstr" && !isLayoutRoomViewName(v)) {
        return QString();
    }

    QString libPath = getCurrentLibraryPath();
    if(!QFileInfo(libPath).exists() || !QFileInfo(libPath).isFile()) {
        return QString();
    }

    return libPath;
}

/*!*******************************************************************************************************************
 * \brief Returns currently selected project union (nested projects) name.
 **********************************************************************************************************************/
QString MainWindow::getCurrentUnionName() const
{
    QString projName;

    QList<QTreeWidgetItem*> projects = m_ui->treeLibs->selectedItems();
    if(!projects.count()) {
        return projName;
    }

    QTreeWidgetItem *projId = projects.first();
    if(!projId) {
        return projName;
    }

    if(!projId->childCount()) {
        return projName;
    }

    if(projId->text(0).isEmpty()) {
        return projName;
    }

    projName = projId->text(0);

    return projName;
}

/*!*******************************************************************************************************************
 * \brief Returns currently selected name of category.
 **********************************************************************************************************************/
QString MainWindow::getCurrentCategoryName() const
{
    QString catName;

    QList<QTreeWidgetItem*> catItems = m_ui->listCategories->selectedItems();
    if(!catItems.count()) {
        return catName;
    }

    QTreeWidgetItem *catId = catItems.first();
    if(!catId) {
        return catName;
    }

    if(catId->text(0).isEmpty()) {
        return catName;
    }

    catName = catId->text(0);

    return catName;
}

/*!*******************************************************************************************************************
 * \brief Returns currently selected project (library) name.
 **********************************************************************************************************************/
QString MainWindow::getCurrentLibraryName() const
{
    QString projName;

    QList<QTreeWidgetItem*> projects = m_ui->treeLibs->selectedItems();
    if(!projects.count()) {
        return projName;
    }

    QTreeWidgetItem *projId = projects.first();
    if(!projId) {
        return projName;
    }

    if(projId->text(0).isEmpty() || projId->childCount() > 0) {
        return projName;
    }

    projName = projId->text(0);

    return projName;
}

QStringList MainWindow::registeredLibraryNames() const
{
    QStringList names;

    const QMap<QString, QString> libraries = getCurrentLibraries();
    for (auto it = libraries.constBegin(); it != libraries.constEnd(); ++it) {
        const QString libraryName = it.key().trimmed();
        if (!libraryName.isEmpty() && !names.contains(libraryName)) {
            names << libraryName;
        }
    }

    for (int i = 0; i < m_ui->treeLibs->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *item = m_ui->treeLibs->topLevelItem(i);
        if (!item) {
            continue;
        }
        const QString libraryName = item->text(0).trimmed();
        if (!libraryName.isEmpty() && !names.contains(libraryName)) {
            names << libraryName;
        }
    }

    names.sort();
    return names;
}

/*!*******************************************************************************************************************
 * \brief Returns currently selected group (cell) name.
 **********************************************************************************************************************/
QString MainWindow::getCurrentGroupName() const
{
    QString groupName;

    QList<QListWidgetItem*> groupItems = m_ui->listGroups->selectedItems();
    if(!groupItems.count()) {
        return groupName;
    }

    QListWidgetItem *groupId = groupItems.first();
    if(!groupId) {
        return groupName;
    }

    if(groupId->text().isEmpty()) {
        return groupName;
    }

    groupName = groupId->text();

    return groupName;
}

/*!*******************************************************************************************************************
 * \brief Returns currently selected view name.
 **********************************************************************************************************************/
QString MainWindow::getCurrentViewName() const
{
    QList<QTreeWidgetItem*> items = m_ui->listViews->selectedItems();
    if (items.isEmpty()) {
        return QString();
    }

    QTreeWidgetItem *item = items.first();
    if (!item) {
        return QString();
    }

    return item->text(0);
}

/*!*******************************************************************************************************************
 * \brief Returns path associated with the currently selected group.
 *
 * In KLayout .lib based workflow groups are cells inside a layout file rather than
 * directories on disk. Therefore this function returns the current library file path.
 *
 * \param viewName        Name of the view.
 * \param toBeCreated     Unused for file-based libraries.
 *
 * \return Absolute path to the current library file or empty string on failure.
 **********************************************************************************************************************/
QString MainWindow::getCurrentGroupPath(const QString &viewName, bool toBeCreated)
{
    Q_UNUSED(viewName);
    Q_UNUSED(toBeCreated);

    QString libPath = getCurrentLibraryPath();
    if(!QFileInfo(libPath).exists() || !QFileInfo(libPath).isFile()) {
        return QString();
    }

    return libPath;
}

/*!*******************************************************************************************************************
 * \brief Returns absolute path of the currently selected library file.
 **********************************************************************************************************************/
QString MainWindow::getCurrentLibraryPath() const
{
    QList<QTreeWidgetItem*> libItems = m_ui->treeLibs->selectedItems();
    if(!libItems.count()) {
        return QString();
    }

    QTreeWidgetItem *libItem = libItems.first();
    if(!libItem) {
        return QString();
    }

    return getLibraryPath(libItem->text(0));
}

/*!*******************************************************************************************************************
 * \brief Returns absolute documentation file path based on the given library path and document name.
 * \param docName      Name of the document.
 **********************************************************************************************************************/
QString MainWindow::getCurrentDocumentFilePath(const QString &docName) const
{
    QString libPath = getCurrentLibraryPath();
    QString docPath = QDir::toNativeSeparators(libPath + "/doc/" + docName);

    if(QFileInfo(docPath).exists()) {
        return docPath;
    }

    return "";
}

/*!*******************************************************************************************************************
 * \brief Returns group (cell) list for the given library file.
 *
 * In file-based KLayout .lib workflow the library points to a specific view file,
 * therefore the group name is derived from the file base name.
 *
 * Example:
 *   ./sg13g2_stdcell/gds/sg13g2_stdcell.gds -> group "sg13g2_stdcell"
 *
 * \param libPath      Path to the library view file.
 *
 * \return List containing a single group name or empty list on failure.
 **********************************************************************************************************************/
QStringList MainWindow::getCurrentGroups(const QString &libName) const
{
    QStringList groups;

    if(libName.isEmpty()) {
        return groups;
    }

    const QMap<QString, PropertyItem*> propItems = m_properties->getMap();
    const QString prefix = getLibraryKeyPrefix() + libName + "/";

    for(auto it = propItems.constBegin(); it != propItems.constEnd(); ++it) {
        const QString key = it.key();
        if(!key.startsWith(prefix)) {
            continue;
        }

        const QString tail = key.mid(prefix.length()).trimmed();
        if(tail.isEmpty()) {
            continue;
        }

        const int pos = tail.indexOf('/');
        if(pos < 0) {
            continue;
        }

        const QString groupName = tail.left(pos).trimmed();
        if(groupName.isEmpty()) {
            continue;
        }

        if(!groups.contains(groupName)) {
            groups << groupName;
        }
    }

    if(hasWildcardDefineForLibrary(libName)) {
        for(const QString &groupName : discoverCellNamesFromDisk(libName)) {
            if(!groups.contains(groupName)) {
                groups << groupName;
            }
        }
    }

    groups.sort();
    return groups;
}

/*!*******************************************************************************************************************
 * \brief Returns valid views for the given library/cell.
 *
 * In KLayout .lib based workflow a library is backed by a single layout file,
 * so the available view is derived from the file extension.
 *
 * \param libPath      Path to the layout file.
 * \param groupName    Name of group (cell) to query views for.
 **********************************************************************************************************************/
QStringList MainWindow::getCurrentViews(const QString &libName, const QString &groupName) const
{
    QStringList views;

    if(libName.isEmpty() || groupName.isEmpty()) {
        return views;
    }

    const QMap<QString, PropertyItem*> propItems = m_properties->getMap();
    const QString prefix = getLibraryKeyPrefix() + libName + "/" + groupName + "/";

    for(auto it = propItems.constBegin(); it != propItems.constEnd(); ++it) {
        const QString key = it.key();
        if(!key.startsWith(prefix)) {
            continue;
        }

        const QString viewName = key.mid(prefix.length()).trimmed();
        if(viewName.isEmpty()) {
            continue;
        }

        if(!views.contains(viewName)) {
            views << viewName;
        }
    }

    for(const QString &viewName : discoverViewNamesFromDisk(libName, groupName)) {
        if(!views.contains(viewName)) {
            views << viewName;
        }
    }

    views.sort();
    return views;
}

/*!*******************************************************************************************************************
 * \brief Returns absolute directory path of the currently loaded project file.
 *
 * \return Absolute project directory path or empty string if no project file is loaded.
 **********************************************************************************************************************/
QString MainWindow::getCurrentProjectDirectory() const
{
    if(m_currentProjFile.isEmpty()) {
        return QString();
    }

    QFileInfo fi(m_currentProjFile);
    if(!fi.exists() || !fi.isFile()) {
        return QString();
    }

    return QDir::toNativeSeparators(fi.absolutePath());
}

/*!*******************************************************************************************************************
 * \brief Searches recursively for documentation files inside a library directory.
 * \param libraryDir   Absolute path to the selected library root (Read Path).
 *
 * \return Sorted list of absolute file paths (pdf, htm, html, md).
 **********************************************************************************************************************/
QStringList MainWindow::findLibraryDocuments(const QString &libraryDir) const
{
    QStringList result;

    if(libraryDir.isEmpty() || !QFileInfo(libraryDir).isDir()) {
        return result;
    }

    static const QStringList kPatterns = {
        QStringLiteral("*.pdf"),
        QStringLiteral("*.htm"),
        QStringLiteral("*.html"),
        QStringLiteral("*.md")
    };

    QDirIterator it(libraryDir, kPatterns, QDir::Files, QDirIterator::Subdirectories);
    while(it.hasNext()) {
        const QString filePath = QDir::toNativeSeparators(it.next());
        if(!result.contains(filePath, Qt::CaseInsensitive)) {
            result << filePath;
        }
    }

    result.sort(Qt::CaseInsensitive);
    return result;
}

/*!*******************************************************************************************************************
 * \brief Adds documents for the given library into the document tree widget.
 *
 * Recursively collects pdf/htm/html/md files under the library root (\p libPath).
 * Other libraries in the same project are not scanned.
 *
 * \param libPath      Library root derived from the selected treeLibs item.
 **********************************************************************************************************************/
void MainWindow::loadDocuments(const QString &libPath)
{
    m_ui->listDocumentation->clear();

    if(libPath.isEmpty() || !QFileInfo(libPath).isDir()) {
        return;
    }

    if(libmanAutomatedTestRun()) {
        return;
    }

    const QStringList docFiles = findLibraryDocuments(libPath);
    if(docFiles.isEmpty()) {
        return;
    }

    foreach(const QString &absPath, docFiles) {
        QFileInfo fi(absPath);

        QTreeWidgetItem *docItem = new QTreeWidgetItem;
        docItem->setText(0, fi.fileName());
        docItem->setToolTip(0, absPath);
        docItem->setData(0, RoleDocumentPath, absPath);

        const QString suffix = fi.suffix().toLower();
        if(suffix == QStringLiteral("pdf")) {
            docItem->setIcon(0, QIcon(QStringLiteral(":/icons/pdf.svg")));
        }
        else {
            docItem->setIcon(0, QIcon(QStringLiteral(":/icons/info.svg")));
        }

        m_ui->listDocumentation->addTopLevelItem(docItem);
    }

#if QT_VERSION >= 0x050000
    m_ui->listDocumentation->sortByColumn(0, Qt::AscendingOrder);
#else
    m_ui->listDocumentation->sortByColumn(0);
#endif

    m_ui->listDocumentation->resizeColumnToContents(0);
}

/*!*******************************************************************************************************************
 * \brief Clears all UI widgets that depend on currently selected library.
 *
 * This function is used when no library is selected anymore, for example after
 * clicking on an empty area in the library tree widget. It resets lists,
 * search fields and related action states so that stale data is not shown.
 **********************************************************************************************************************/
void MainWindow::clearLibrarySelectionDependentViews()
{
    m_itemText.clear();

    m_ui->txtLibSearch->clear();
    m_ui->txtCatSearch->clear();
    m_ui->txtCellSearch->clear();
    m_ui->txtViewSearch->clear();

    m_ui->listGroups->clear();
    m_ui->listViews->clear();
    m_ui->listDocumentation->clear();
    m_ui->listCategories->clear();

    updateLibraryActionStates();
    refreshSnapshot();
}

/*!*******************************************************************************************************************
 * \brief Enables toolbar actions based on the current library/cell selection.
 **********************************************************************************************************************/
void MainWindow::updateLibraryActionStates()
{
    const QString libName = getCurrentLibraryName();
    const bool hasLibrary = !libName.isEmpty();
    const QString libRoot = hasLibrary ? getLibraryPath(libName) : QString();
    const bool hasLibraryRoot = !libRoot.isEmpty() && QFileInfo(libRoot).exists();
    const bool hasCell = !getCurrentGroupName().isEmpty();

    m_ui->actionGroup->setEnabled(hasLibrary);
    m_ui->actionUnion->setEnabled(hasLibrary && hasLibraryRoot && hasCell);
    m_ui->actionCategory->setEnabled(hasLibrary && hasLibraryRoot);
}

/*!*******************************************************************************************************************
 * \brief Enables drag-and-drop of supported layout files onto library and cell panes.
 **********************************************************************************************************************/
void MainWindow::setupDragAndDrop()
{
    m_ui->treeLibs->setAcceptDrops(true);
    m_ui->treeLibs->viewport()->setAcceptDrops(true);
    m_ui->listGroups->setAcceptDrops(true);
    m_ui->listGroups->viewport()->setAcceptDrops(true);

    m_ui->treeLibs->viewport()->installEventFilter(this);
    m_ui->listGroups->viewport()->installEventFilter(this);
}

/*!*******************************************************************************************************************
 * \brief Handles library tree selection changes.
 *
 * If the current library selection becomes empty, all dependent widgets on the
 * right side are cleared to avoid showing stale groups, views, categories and
 * documents from previously selected library.
 **********************************************************************************************************************/
void MainWindow::on_treeLibs_itemSelectionChanged()
{
    const QList<QTreeWidgetItem*> items = m_ui->treeLibs->selectedItems();
    if(items.isEmpty()) {
        clearLibrarySelectionDependentViews();
        return;
    }

    QTreeWidgetItem *item = items.first();
    if(!item) {
        clearLibrarySelectionDependentViews();
        return;
    }

    if(item->childCount()) {
        clearLibrarySelectionDependentViews();
        return;
    }

    updateLibraryActionStates();
}

/*!*******************************************************************************************************************
 * \brief Adds categories for the given project (library) into the category tree widget.
 * \param libPath      Path to the library, where category is located.
 **********************************************************************************************************************/
void MainWindow::loadCategories(const QString &libPath)
{
    m_ui->listCategories->clear();

    if(!QFileInfo(libPath).isDir()) {
        return;
    }

    QStringList formats;
    formats<<"*.group";

    QDir catDir(libPath);
    catDir.setNameFilters(formats);

    QStringList fileList = catDir.entryList();
    foreach(QString catName, fileList) {
        QTreeWidgetItem *catItem = new QTreeWidgetItem;
        catItem->setText(0, QFileInfo(catName).completeBaseName());
        m_ui->listCategories->addTopLevelItem(catItem);
    }

#if QT_VERSION >= 0x050000
    m_ui->listCategories->sortByColumn(0, Qt::AscendingOrder);
#else
    m_ui->listCategories->sortByColumn(0);
#endif

    m_ui->listCategories->resizeColumnToContents(0);
}

/*!*******************************************************************************************************************
 * \brief Adds group (cell) for the given library into the group list widget.
 *
 * In file-based KLayout .lib workflow the group name is derived from the file base name.
 *
 * \param libPath      Path to the library view file.
 **********************************************************************************************************************/
void MainWindow::loadGroups(const QString &libName)
{
    m_ui->listGroups->clear();
    m_ui->listViews->clear();

    QStringList groups = getCurrentGroups(libName);

    foreach(const QString &groupName, groups) {
        QListWidgetItem *groupItem = new QListWidgetItem;
        groupItem->setText(groupName);
        groupItem->setFlags(groupItem->flags() | Qt::ItemIsEditable);
        m_ui->listGroups->addItem(groupItem);
    }

    m_ui->listGroups->sortItems();
    refreshSnapshot();
}

/*!*******************************************************************************************************************
 * \brief Adds views for the given group (cell) into the view tree widget.
 *
 * In KLayout .lib based workflow the library is represented by a single layout file.
 * Therefore the view type is derived from the file extension and the selected cell
 * is opened from that same file.
 *
 * \param libPath      Path to the library layout file.
 * \param groupName    Name of group (cell) to load its view(s).
 **********************************************************************************************************************/
void MainWindow::loadViews(const QString &libName, const QString &groupName)
{
    m_ui->listViews->clear();
    m_ui->listViews->setHeaderHidden(true);
    m_ui->listViews->setRootIsDecorated(true);

    const QStringList views = getCurrentViews(libName, groupName);

    foreach(const QString &viewName, views) {
        const QString viewPath = getViewPath(libName, groupName, viewName);
        if(viewPath.isEmpty()) {
            continue;
        }

        QTreeWidgetItem *viewItem = new QTreeWidgetItem(m_ui->listViews);
        viewItem->setText(0, viewName);

        if(viewName == "gds") {
            viewItem->setData(0, RoleType, ItemViewGds);
            viewItem->setData(0, RoleGdsPath, viewPath);
            applyViewTreeIcon(viewItem, viewName);
            viewItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        }
        else if(viewName == "lstr") {
            viewItem->setData(0, RoleType, ItemViewLStream);
            viewItem->setData(0, RoleLStreamPath, viewPath);
            applyViewTreeIcon(viewItem, viewName);
            viewItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        }
        else if(viewName == "oas" || viewName == "oasis") {
            viewItem->setData(0, RoleType, ItemViewOas);
            viewItem->setData(0, RoleOasPath, viewPath);
            applyViewTreeIcon(viewItem, viewName);
            viewItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        }
        else if(isEmSetupViewName(viewName)) {
            configureEmSetupViewTreeItem(viewItem, viewPath);
        }
        else if(isRoomViewName(viewName)) {
            configureCoreViewTreeItem(viewItem, viewName, viewPath);
            applyCoreViewLockPresentation(viewItem, viewName, viewPath);
        }
        else {
            applyViewTreeIcon(viewItem, viewName);
        }
    }

    m_ui->listViews->sortItems(0, Qt::AscendingOrder);
    syncCoreLockWatches();

    const QString emPath = getViewPath(libName, groupName, QStringLiteral("emmodel"));
    m_emModelWatchPath = emPath;
    m_emModelWatchMtime = (!emPath.isEmpty() && QFileInfo::exists(emPath))
            ? QFileInfo(emPath).lastModified()
            : QDateTime();

    refreshSnapshot();
}

/*!*******************************************************************************************************************
 * \brief Adds project (libraries) into the project tree widget from the LibMan settings.
 **********************************************************************************************************************/
void MainWindow::loadLibraries()
{
    m_ui->treeLibs->clear();

    QMap<QString, QString> libraries = getCurrentLibraries();
    QMap<QString, QString>::const_iterator it;

    for(it = libraries.constBegin(); it != libraries.constEnd(); ++it) {
        QString libName = it.key();
        if(libName.isEmpty()) {
            continue;
        }

        QTreeWidgetItem *item = new QTreeWidgetItem;
        item->setText(0, libName);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        m_ui->treeLibs->addTopLevelItem(item);
    }

#if QT_VERSION >= 0x050000
    m_ui->treeLibs->sortByColumn(0, Qt::AscendingOrder);
#else
    m_ui->treeLibs->sortByColumn(0);
#endif
}

void MainWindow::populateLibraryBrowser(const QString &libName)
{
    if (libName.isEmpty()) {
        return;
    }

    m_itemText = libName;
    m_ui->txtLibSearch->setText(libName);

    loadGroups(libName);

    const QString libPath = getLibraryPath(libName);
    loadDocuments(libPath);
    loadCategories(libPath);
    updateLibraryActionStates();
}

/*!*******************************************************************************************************************
 * \brief Adds given nested projects (libraries) into the project tree widget.
 * \param combinedLibs     Map of combined libraries, where key - is name of the library, values - is library path.
 **********************************************************************************************************************/
void MainWindow::loadCombinedLibs(const QMap<QString, QStringList> &combinedLibs)
{
    QMap<QString, QStringList>::const_iterator it;
    for(it = combinedLibs.constBegin(); it != combinedLibs.constEnd(); it++) {
        QString groupName = it.key();

        QTreeWidgetItem *groupId = new QTreeWidgetItem;
        groupId->setText(0, groupName);
        groupId->setFlags(groupId->flags() | Qt::ItemIsEditable);
        m_ui->treeLibs->addTopLevelItem(groupId);

        QStringList groupLibs = it.value();
        foreach(const QString libName, groupLibs) {
            for(int i = 0; i < m_ui->treeLibs->topLevelItemCount(); ++i) {
                QTreeWidgetItem *item = m_ui->treeLibs->topLevelItem(i);
                if(!item) {
                    continue;
                }

                QString curProjName = item->text(0);

                if(curProjName == libName) {
                    QTreeWidgetItem *childId = new QTreeWidgetItem;
                    childId->setText(0, libName);
                    childId->setFlags(childId->flags() | Qt::ItemIsEditable);

                    m_ui->treeLibs->takeTopLevelItem(i);

                    groupId->addChild(childId);
                }
            }
        }
    }
}

/*!*******************************************************************************************************************
 * \brief Slot to load all groups (cells) for selected library into the group list widget.
 * \param item       Pointer to list item library.
 **********************************************************************************************************************/
void MainWindow::on_treeLibs_itemClicked(QTreeWidgetItem *item, int)
{
    m_itemText = "";

    if(!item) {
        return;
    }

    if(item->childCount()) {
        return;
    }

    if(m_ui->treeLibs->selectedItems().count() > 1) {
        return;
    }

    m_itemText = item->text(0);

    m_ui->txtLibSearch->clear();
    m_ui->txtCatSearch->clear();
    m_ui->txtCellSearch->clear();
    m_ui->txtViewSearch->clear();

    populateLibraryBrowser(item->text(0));
}

/*!*******************************************************************************************************************
 * \brief Slot to load all views for selected group (cell) into the view list widget.
 * \param item       Pointer to list item group.
 **********************************************************************************************************************/
void MainWindow::on_listGroups_itemClicked(QListWidgetItem *item)
{
    if(!item) {
        return;
    }

    if(m_ui->listGroups->selectedItems().count() > 1) {
        return;
    }

    m_itemText = item->text();

    m_ui->txtCellSearch->clear();
    m_ui->txtViewSearch->clear();
    m_ui->listViews->clear();

    QList<QTreeWidgetItem*> libItems = m_ui->treeLibs->selectedItems();
    if(!libItems.count()) {
        return;
    }

    QTreeWidgetItem *libItem = libItems.first();
    if(!libItem) {
        return;
    }

    m_ui->txtCellSearch->setText(item->text());

    loadViews(libItem->text(0), item->text());

    updateLibraryActionStates();
}

/*!*******************************************************************************************************************
 * \brief Slot to execute a tool to view ctegory content.
 * \param item       Pointer to tree item category.
 * \param column     Column where double click event happened.
 **********************************************************************************************************************/
void MainWindow::on_listCategories_itemDoubleClicked(QTreeWidgetItem *item, int column)
{
    if(!item) {
        return;
    }

    m_itemText = item->text(column);

    m_ui->txtCatSearch->setText(m_itemText);

    QString tool = m_properties->get<QString>("Editor");
    if(tool.isEmpty()) {
        error(QString("Please specify tool first."));
        return;
    }

    QString libPath = getCurrentLibraryPath();
    if(!QFileInfo(libPath).exists()) {
        return;
    }

    QString catFile = QDir::toNativeSeparators(libPath + "/" + m_itemText + ".group");
    if(!QFileInfo(catFile).exists()) {
        return;
    }

    QProcess proc;
    QStringList args;
    args<<catFile;

    proc.startDetached(tool, args);
}


/*!*******************************************************************************************************************
 * \brief Opens selected view using configured external tool.
 *        For GDS it generates a temporary KLayout script and opens either the whole file or a specific cell.
 * \param item       Pointer to a clicked tree item.
 * \param            Column (unused).
 **********************************************************************************************************************/
void MainWindow::on_listViews_itemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

    if(!item) {
        return;
    }

    m_itemText = item->text(0);
    m_ui->txtViewSearch->setText(item->text(0));

    const int type = item->data(0, RoleType).toInt();

    QString viewName;   // "gds" / "oas" / "cdl" / ...
    QString viewPath;   // file path for gds/oas or normal view file
    QString cellName;   // for hierarchy nodes

    // ------------------------------------------------------------
    // Root items: "gds" / "oas" / "lstr"
    // ------------------------------------------------------------
    if(type == ItemViewGds && item->text(0) == "gds") {
        viewName = "gds";
        viewPath = item->data(0, RoleGdsPath).toString();
    }
    else if(type == ItemViewOas && (item->text(0) == "oas" || item->text(0) == "oasis")) {
        viewName = "oas";
        viewPath = item->data(0, RoleOasPath).toString();
    }
    else if(type == ItemViewLStream && (item->text(0) == "lstr" || item->text(0) == "lstream")) {
        viewName = "lstr";
        viewPath = item->data(0, RoleLStreamPath).toString();
    }
    else if(type == ItemViewCore) {
        viewName = item->text(0);
        viewPath = item->data(0, RoleCorePath).toString();
    }
    else if(type == ItemCell) {

        // find top view root ("gds" or "oas" or "lstr")
        QTreeWidgetItem *p = item->parent();
        while(p && p->parent()) {
            p = p->parent();
        }
        if(!p) {
            return;
        }

        const QString rootName = p->text(0);

        if(rootName == "gds") {
            viewName = "gds";
            viewPath = p->data(0, RoleGdsPath).toString();
        }
        else if(rootName == "oas" || rootName == "oasis") {
            viewName = "oas";
            viewPath = p->data(0, RoleOasPath).toString();
        }
        else if(rootName == "lstr" || rootName == "lstream") {
            viewName = "lstr";
            viewPath = p->data(0, RoleLStreamPath).toString();
        }
        else if(p->data(0, RoleType).toInt() == ItemViewCore) {
            viewName = rootName;
            viewPath = p->data(0, RoleCorePath).toString();
        }
        else {
            return;
        }

        cellName = item->data(0, RoleCellName).toString();

        if(viewPath.isEmpty() || cellName.isEmpty()) {
            return;
        }
    }
    // ------------------------------------------------------------
    // Normal views: cdl/spice/verilog/...
    // ------------------------------------------------------------
    else if(type == ItemViewEmSetup || type == ItemEmSetupVariant
            || isEmSetupViewName(item->text(0))) {
        viewName = QStringLiteral("emsetup");
        if(type == ItemEmSetupVariant) {
            viewPath = item->data(0, RoleEmSetupPath).toString();
        } else {
            viewPath = item->data(0, RoleEmSetupPath).toString();
            if(viewPath.isEmpty()) {
                viewPath = getViewPath(getCurrentLibraryName(), getCurrentGroupName(), viewName);
            }
        }
    }
    else {
        viewName = item->text(0);
        if(viewName.isEmpty()) {
            return;
        }

        viewPath = getViewPath(getCurrentLibraryName(), getCurrentGroupName(), viewName);
    }

    if(viewPath.isEmpty() || !QFileInfo(viewPath).exists()) {
        error(QString("Failed to find view '%1'").arg(viewPath));
        return;
    }

    // ------------------------------------------------------------
    // emsetup: open model.py (EMStudio if configured) or the variant folder
    // ------------------------------------------------------------
    if(isEmSetupViewName(viewName) || type == ItemViewEmSetup || type == ItemEmSetupVariant) {
        QString modelPath;
        QFileInfo fi(viewPath);
        if(fi.isFile() && fi.fileName().endsWith(QLatin1String(".py"), Qt::CaseInsensitive)) {
            modelPath = fi.absoluteFilePath();
        } else if(fi.isDir()) {
            const QString direct = QDir(viewPath).filePath(QStringLiteral("model.py"));
            if(QFileInfo::exists(direct)) {
                modelPath = direct;
            } else {
                const QString nominal = QDir(viewPath).filePath(
                    emSetupDefaultVariantName() + QStringLiteral("/model.py"));
                if(QFileInfo::exists(nominal)) {
                    modelPath = nominal;
                } else {
                    const QFileInfoList pys = QDir(viewPath).entryInfoList(
                        QStringList() << QStringLiteral("*.py"), QDir::Files, QDir::Name);
                    if(!pys.isEmpty()) {
                        modelPath = pys.first().absoluteFilePath();
                    } else {
                        const QFileInfoList nested = QDir(viewPath).entryInfoList(
                            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                        for(const QFileInfo &sub : nested) {
                            const QString cand = QDir(sub.absoluteFilePath()).filePath(QStringLiteral("model.py"));
                            if(QFileInfo::exists(cand)) {
                                modelPath = cand;
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Tool Manager maps view "emsetup" → EMStudio; nested items (nominal, …)
        // keep viewName = emsetup so the same tool applies.
        QString emstudio = getToolByView(QStringLiteral("emsetup"));
        if(emstudio.isEmpty() && m_properties->exists(QStringLiteral("EMSTUDIO"))) {
            emstudio = m_properties->get<QString>(QStringLiteral("EMSTUDIO")).trimmed();
        }
        if(emstudio.isEmpty()) {
            emstudio = QStandardPaths::findExecutable(QStringLiteral("EMStudio"));
#ifdef Q_OS_WIN
            if(emstudio.isEmpty()) {
                emstudio = QStandardPaths::findExecutable(QStringLiteral("EMStudio.exe"));
            }
#endif
        }
        if(!modelPath.isEmpty() && !emstudio.isEmpty() && QFileInfo::exists(emstudio)) {
            if(!QProcess::startDetached(emstudio, QStringList() << modelPath)) {
                error(tr("Failed to start EMStudio:\n%1").arg(emstudio), false);
            }
            return;
        }
        if(!modelPath.isEmpty()) {
            info(tr("EM setup model: %1\n"
                    "Configure the EM tool for view 'emsetup' in Tool Manager "
                    "(or set property EMSTUDIO) to open it.")
                     .arg(QDir::toNativeSeparators(modelPath)));
            return;
        }
        info(tr("EM setup folder: %1").arg(QDir::toNativeSeparators(viewPath)));
        return;
    }

    QString tool = getToolByView(viewName);
    if(tool.isEmpty()) {
        error(QString("Please specify tool first."));
        return;
    }

    // ------------------------------------------------------------
    // Layout handling via KLayout server: GDS + OAS (same behavior)
    // ------------------------------------------------------------
    if(viewName == "gds" || viewName == "oas" || viewName == "lstr" || isLayoutRoomViewName(viewName)) {

        QStringList bridgeErrors;
        const QString klayoutPath = layoutPathForKLayout(viewName, viewPath, &bridgeErrors);
        if(klayoutPath.isEmpty()) {
            if(!bridgeErrors.isEmpty()) {
                error(bridgeErrors.join(QLatin1Char('\n')), false);
            }
            else {
                error(QString("Failed to prepare layout file for KLayout: %1").arg(viewPath), false);
            }
            return;
        }

        // Root item: "gds", "oas", "lstr", or layout ROOM view
        if(type == ItemViewGds || type == ItemViewOas || type == ItemViewLStream || type == ItemViewCore) {

            const QString groupName = getCurrentGroupName();
            const QString rootCell = preferredKLayoutCellForRoot(klayoutPath, groupName);
            const bool serverWasRunning = isKLayoutServerRunning();

            if(serverWasRunning) {
                if(!rootCell.isEmpty()) {
                    sendKLayoutSelectRequest(klayoutPath, rootCell);
                }
                return;
            }

            if(!ensureKLayoutServerRunning(tool)) {
                return;
            }

            sendKLayoutOpenRequest(klayoutPath, rootCell);
            return;
        }

        if(!ensureKLayoutServerRunning(tool)) {
            return;
        }

        sendKLayoutOpenRequest(klayoutPath, cellName);
        return;
    }

    // ------------------------------------------------------------
    // Non-layout views: original behavior
    // ------------------------------------------------------------
    if(isSchematicLikeView(viewName)) {
        launchSchematicTool(tool, viewPath);
    }
    else {
        QProcess::startDetached(tool, QStringList() << viewPath);
    }
}

/*!*******************************************************************************************************************
 * \brief Filters events for the Views tree widget to suppress default expand/collapse behavior on double click.
 *
 * This event filter prevents QTreeWidget from automatically expanding or collapsing
 * layout view nodes (gds/oas/lstr/layout) and their hierarchy cell nodes on double click.
 * Double click is reserved exclusively for opening the corresponding view or cell in KLayout.
 *
 * \param obj      Object which received the event.
 * \param event    Event instance.
 *
 * \return true if the event was handled and should not be propagated further,
 *         false to allow default event processing.
 **********************************************************************************************************************/
bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_ui->editPythonCommand && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            // Shift+Enter — new line; Enter — run (Virtuoso-style).
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                return false;
            }
            runPythonCommand();
            return true;
        }
        // Up/Down: command history (like a shell). Within multiline text, only at
        // first/last line so arrow keys still move the cursor otherwise.
        if (keyEvent->modifiers() == Qt::NoModifier
            && (keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down)) {
            const QTextCursor cursor = m_ui->editPythonCommand->textCursor();
            const int block = cursor.blockNumber();
            const int blocks = m_ui->editPythonCommand->document()->blockCount();
            const bool atFirst = (block <= 0);
            const bool atLast = (block >= blocks - 1);
            if (keyEvent->key() == Qt::Key_Up && atFirst) {
                return recallPythonHistory(-1);
            }
            if (keyEvent->key() == Qt::Key_Down && atLast) {
                return recallPythonHistory(+1);
            }
        }
    }

    if(obj == m_ui->textMessages->viewport()) {
        if(event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QString anchor = m_ui->textMessages->anchorAt(mouseEvent->pos());
            m_ui->textMessages->viewport()->setCursor(anchor.isEmpty() ? Qt::IBeamCursor : Qt::PointingHandCursor);
        }
        else if(event->type() == QEvent::Leave) {
            m_ui->textMessages->viewport()->unsetCursor();
        }
        else if(event->type() == QEvent::MouseButtonRelease) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QString anchor = m_ui->textMessages->anchorAt(mouseEvent->pos());
            if(!anchor.isEmpty()) {
                onLogAnchorClicked(QUrl(anchor));
                return true;
            }
        }
    }

    if (obj == m_ui->listViews->viewport() && event->type() == QEvent::MouseButtonDblClick) {
        auto *mouseEvent = static_cast<QMouseEvent*>(event);
        QTreeWidgetItem *item = m_ui->listViews->itemAt(mouseEvent->pos());
        if (isLayoutViewTreeItem(item)) {
            on_listViews_itemDoubleClicked(item, m_ui->listViews->columnAt(mouseEvent->pos().x()));
            return true;
        }
    }

    const bool isDropTarget =
        obj == m_ui->treeLibs->viewport() ||
        obj == m_ui->listGroups->viewport();

    if(isDropTarget) {
        if(event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto *dragEvent = static_cast<QDragEnterEvent*>(event);
            if(isSupportedViewDrop(dragEvent->mimeData())) {
                dragEvent->acceptProposedAction();
            }
            else {
                dragEvent->ignore();
            }
            return true;
        }

        if(event->type() == QEvent::Drop) {
            handleViewFileDrop(static_cast<QDropEvent*>(event));
            return true;
        }
    }

    return QMainWindow::eventFilter(obj, event);
}

/*!*******************************************************************************************************************
 * \brief Recursively hides/shows items based on filter, without populating any lazy data.
 *
 * Works only on items that already exist in the tree (already expanded/filled).
 * If any child matches, its parents become visible too.
 *
 * \param item     Item to process.
 * \param filter   Search text.
 * \return true if item or any of its existing children matches.
 **********************************************************************************************************************/
bool MainWindow::filterViewsTreeItemNoPopulate(QTreeWidgetItem *item, const QString &filter)
{
    if (!item) {
        return false;
    }

    const bool match = filter.isEmpty() || item->text(0).contains(filter, Qt::CaseInsensitive);

    bool childMatch = false;
    for (int i = 0; i < item->childCount(); ++i) {
        if (filterViewsTreeItemNoPopulate(item->child(i), filter)) {
            childMatch = true;
        }
    }

    const bool visible = filter.isEmpty() || match || childMatch;
    item->setHidden(!visible);

    // optional UX: auto-expand only already-existing branches that have matches
    if (!filter.isEmpty() && childMatch) {
        item->setExpanded(true);
    }

    return (match || childMatch);
}

/*!*******************************************************************************************************************
 * \brief Handles click on an item in the Views tree widget.
 *        Stores clicked item text and updates the View filter line edit.
 * \param item       Pointer to clicked tree item.
 * \param column     Column index where click happened.
 **********************************************************************************************************************/
void MainWindow::on_txtViewSearch_textEdited(const QString &filter)
{
    for (int i = 0; i < m_ui->listViews->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = m_ui->listViews->topLevelItem(i);
        filterViewsTreeItemNoPopulate(top, filter);
    }
}

/*!*******************************************************************************************************************
 * \brief Slot is to load groups (cells) located into the specified category file.
 * \param item       Pointer to tree item category.
 **********************************************************************************************************************/
void MainWindow::on_listCategories_itemClicked(QTreeWidgetItem *item)
{
    if(!item) {
        return;
    }

    m_itemText = item->text(0);

    QString libPath = getCurrentLibraryPath();
    QStringList groups = readLibraryCategories(libPath, item->text(0));

    m_ui->txtCatSearch->setText(item->text(0));

    m_ui->listGroups->clear();
    m_ui->listViews->clear();

    foreach(const QString &groupName, groups) {
        QListWidgetItem *groupItem = new QListWidgetItem;
        groupItem->setText(groupName);
        groupItem->setFlags(groupItem->flags() | Qt::ItemIsEditable);
        m_ui->listGroups->addItem(groupItem);
    }

    m_ui->listGroups->sortItems();
    refreshSnapshot();
}

/*!*******************************************************************************************************************
 * \brief Slot is to execute document viewer with specified documentation.
 * \param item       Pointer to tree item document.
 **********************************************************************************************************************/
void MainWindow::on_listDocumentation_itemDoubleClicked(QTreeWidgetItem *item)
{
    if(!item) {
        return;
    }

    m_itemText = item->text(0);

    const QString docName = item->text(0);
    if(docName.isEmpty()) {
        return;
    }

    const QString docPath = item->data(0, RoleDocumentPath).toString();
    if(docPath.isEmpty()) {
        error(QString("Failed to find document '%1'.").arg(docName), false);
        return;
    }

    // HTML and http(s) links: default system browser (not the configured text Editor).
    const QUrl asUrl(docPath);
    const bool isRemoteUrl = asUrl.isValid()
        && (asUrl.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0
            || asUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0);
    const QString suffix = QFileInfo(docName).suffix().toLower();
    const bool isHtml = (suffix == QStringLiteral("htm") || suffix == QStringLiteral("html"));

    if(isRemoteUrl) {
        if(!QDesktopServices::openUrl(asUrl)) {
            error(QString("Failed to open URL '%1' in the default browser.").arg(docPath), false);
        }
        return;
    }

    if(!QFileInfo(docPath).exists()) {
        error(QString("Failed to find document '%1'.").arg(docPath), false);
        return;
    }

    if(isHtml) {
        if(!QDesktopServices::openUrl(QUrl::fromLocalFile(docPath))) {
            error(QString("Failed to open '%1' in the default browser.").arg(docPath), false);
        }
        return;
    }

    const QString tool = getDocumentTool(docName);
    if(tool.isEmpty()) {
        error(QString("Please specify tool first."), false);
        return;
    }

    QProcess proc;
    QStringList args;
    args << docPath;

    proc.startDetached(tool, args);
}

/*!*******************************************************************************************************************
 * \brief Set LibMan state saved and updates the window titel accordingly.
 **********************************************************************************************************************/
void MainWindow::setStateSaved()
{
    m_isStateChanged = false;

    QString title = windowTitle();
    title.remove("*");
    setWindowTitle(title);
}

/*!*******************************************************************************************************************
 * \brief Set LibMan state changed and updates the window titel accordingly.
 ********************************************************************************************************************/
void MainWindow::setStateChanged()
{
    m_isStateChanged = true;

    QString title = windowTitle();
    if(!title.contains("*")) {
        setWindowTitle(title + "*");
    }
}

/*!*******************************************************************************************************************
 * \brief Saves the current project file or executes on_actionSave_As_triggered if no current file specified.
 **********************************************************************************************************************/
void MainWindow::on_actionSave_triggered()
{
    QString projFile = getCurrentProjectFile();
    if(projFile.isEmpty()) {
        on_actionSave_As_triggered();
    }
    else {
        saveProjectFile(projFile);
    }
}

/*!*******************************************************************************************************************
 * \brief Pops up dialog box where user can specify the file to save the library definition.
 **********************************************************************************************************************/
void MainWindow::on_actionSave_As_triggered()
{
    QString workDir = getCurrentWorkingDir();
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Save Project As..."),
                                                    workDir,
                                                    tr("DOOR project (*.projects *.lib);;All files (*)"));
    if(!fileName.isEmpty()) {
        saveProjectFile(fileName);
    }
}

/*!*******************************************************************************************************************
 * \brief Closes LibMan.
 **********************************************************************************************************************/
void MainWindow::on_actionExit_triggered()
{
    close();
}

/*!*******************************************************************************************************************
 * \brief Slot is triggered to show or hide the categories tree widget.
 * \param state      Sate to show (true) or hide (false) the categories tree widget.
 **********************************************************************************************************************/
void MainWindow::on_actionShow_Categories_toggled(bool state)
{
    m_ui->groupCats->setVisible(state);
}

/*!*******************************************************************************************************************
 * \brief Slot is triggered to show or hide the documentation tree widget.
 * \param state      Sate to show (true) or hide (false) the documentation tree widget.
 **********************************************************************************************************************/
void MainWindow::on_actionShow_Documents_toggled(bool state)
{
    m_ui->groupDocs->setVisible(state);
}

void MainWindow::on_actionShow_Snapshot_toggled(bool state)
{
    m_ui->groupSnapshot->setVisible(state);
    if (!state) {
        return;
    }

    const QList<int> sizes = m_ui->splitterView->sizes();
    if (sizes.size() == 2 && sizes.at(1) < 80) {
        const int total = qMax(sizes.at(0) + sizes.at(1), 240);
        const int snapshot = qMax(140, total / 3);
        m_ui->splitterView->setSizes({qMax(80, total - snapshot), snapshot});
    }
    refreshSnapshot();
}

void MainWindow::on_actionShow_Messages_toggled(bool)
{
    updateMessagesPanelVisibility();
}

void MainWindow::on_actionShow_Command_Line_toggled(bool)
{
    updateMessagesPanelVisibility();
}

void MainWindow::on_actionShow_Assistant_toggled(bool)
{
    updateMessagesPanelVisibility();
}

void MainWindow::updateMessagesPanelVisibility()
{
    const bool showMessages = m_ui->actionShow_Messages->isChecked();
    const bool showCommand = m_ui->actionShow_Command_Line->isChecked();
    const bool showAssistant = m_ui->actionShow_Assistant->isChecked();
    m_ui->groupMessages->setVisible(showMessages);
    m_ui->groupCommandLine->setVisible(showCommand);
    m_ui->splitterMessages->setVisible(showMessages || showCommand);
    if (m_ui->groupAssistant) {
        m_ui->groupAssistant->setVisible(showAssistant);
    }
    if (m_ui->splitterBottom) {
        m_ui->splitterBottom->setVisible(showMessages || showCommand || showAssistant);
    }
    if (showMessages && showCommand) {
        const QList<int> sizes = m_ui->splitterMessages->sizes();
        if (sizes.size() == 2 && (sizes.at(0) < 40 || sizes.at(1) < 40)) {
            m_ui->splitterMessages->setSizes({300, 80});
        }
    }
}

void MainWindow::setupAssistant()
{
    if (!m_ui || !m_ui->assistantHost) {
        return;
    }

    m_assistantChat = new AssistantChatPanel(m_ui->assistantHost);
    auto *lay = new QVBoxLayout(m_ui->assistantHost);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_assistantChat);

    m_assistantMcp = new AssistantMcp(this);
    m_roomMcp = new RoomMcpClient(this);
    registerAssistantMcpTools();
    m_assistantChat->setMcp(m_assistantMcp);

    m_assistantAgent = new AssistantAgent(this);
    m_assistantAgent->setMcp(m_assistantMcp);
    configureAssistantAgent();
    m_assistantChat->setAgent(m_assistantAgent);
}

void MainWindow::configureAssistantAgent()
{
    if (!m_assistantAgent) {
        return;
    }
    const QString key = m_properties->exists(QStringLiteral("ASSISTANT_API_KEY"))
                            ? m_properties->get<QString>(QStringLiteral("ASSISTANT_API_KEY"))
                            : QString();
    const QString base = m_properties->exists(QStringLiteral("ASSISTANT_BASE_URL"))
                             ? m_properties->get<QString>(QStringLiteral("ASSISTANT_BASE_URL"))
                             : QStringLiteral("https://api.openai.com/v1");
    const QString model = m_properties->exists(QStringLiteral("ASSISTANT_MODEL"))
                              ? m_properties->get<QString>(QStringLiteral("ASSISTANT_MODEL"))
                              : QStringLiteral("gpt-4o-mini");
    m_assistantAgent->configure(key, base, model);
    if (m_assistantChat) {
        m_assistantChat->refreshModeUi();
    }
}

void MainWindow::persistAssistantSettings()
{
    if (!m_properties) {
        return;
    }

    QSettings settings(getSettingsHeaderName());
    settings.beginGroup(QStringLiteral("Tools"));

    auto savePlain = [&](const QString &key, const QString &fallback = QString()) {
        const QString value = m_properties->exists(key) ? m_properties->get<QString>(key) : fallback;
        if (value.isEmpty() && fallback.isEmpty() && !m_properties->exists(key)) {
            return;
        }
        settings.setValue(key, value);
    };
    savePlain(QStringLiteral("ASSISTANT_BASE_URL"), QStringLiteral("https://api.openai.com/v1"));
    savePlain(QStringLiteral("ASSISTANT_MODEL"), QStringLiteral("gpt-4o-mini"));

    // Never persist cleartext API key — only DPAPI blob for current Windows user.
    settings.remove(QStringLiteral("ASSISTANT_API_KEY"));
    const QString plain = m_properties->exists(QStringLiteral("ASSISTANT_API_KEY"))
                              ? m_properties->get<QString>(QStringLiteral("ASSISTANT_API_KEY")).trimmed()
                              : QString();
    if (plain.isEmpty()) {
        settings.remove(QStringLiteral("ASSISTANT_API_KEY_DPAPI"));
    } else {
        const QString enc = SecureStore::protectToBase64(plain);
        if (!enc.isEmpty()) {
            settings.setValue(QStringLiteral("ASSISTANT_API_KEY_DPAPI"), enc);
        } else {
            qWarning("DOOR: failed to encrypt ASSISTANT_API_KEY (not saved)");
            settings.remove(QStringLiteral("ASSISTANT_API_KEY_DPAPI"));
        }
    }

    settings.endGroup();
}

void MainWindow::clearLoadedPdks()
{
    m_loadedPdks.clear();
}

QSet<QString> MainWindow::pdkMemberLibraryNames() const
{
    QSet<QString> names;
    for (const DoorPdkInfo &pdk : m_loadedPdks) {
        for (const DoorPdkLibrary &lib : pdk.libraries) {
            names.insert(lib.name);
        }
    }
    return names;
}

const DoorPdkInfo *MainWindow::activePdk() const
{
    if (m_loadedPdks.isEmpty()) {
        return nullptr;
    }
    // Prefer PDK that owns the currently selected library.
    const QString cur = getCurrentLibraryName();
    if (!cur.isEmpty()) {
        for (const DoorPdkInfo &pdk : m_loadedPdks) {
            for (const DoorPdkLibrary &lib : pdk.libraries) {
                if (lib.name == cur) {
                    return &pdk;
                }
            }
            if (pdk.name == cur) {
                return &pdk;
            }
        }
    }
    return &m_loadedPdks.first();
}

bool MainWindow::libraryHasIndexedViews(const QString &libName) const
{
    if (libName.isEmpty() || !m_properties) {
        return false;
    }
    if (hasWildcardDefineForLibrary(libName)) {
        return true;
    }
    const QString prefix = getLibraryKeyPrefix() + libName + QLatin1Char('/');
    const QMap<QString, PropertyItem *> propItems = m_properties->getMap();
    for (auto it = propItems.constBegin(); it != propItems.constEnd(); ++it) {
        if (it.key().startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

void MainWindow::indexPdkLibraryContent(const QString &libName, const QString &libRoot)
{
    if (libName.isEmpty() || libRoot.isEmpty() || !m_properties) {
        return;
    }
    // Keep existing ROOM / wildcard design libraries (e.g. converted sg13g2_pr).
    if (hasWildcardDefineForLibrary(libName)) {
        return;
    }
    {
        const QString prefix = getLibraryKeyPrefix() + libName + QLatin1Char('/');
        const QMap<QString, PropertyItem *> propItems = m_properties->getMap();
        for (auto it = propItems.constBegin(); it != propItems.constEnd(); ++it) {
            if (!it.key().startsWith(prefix)) {
                continue;
            }
            const QString path = m_properties->get<QString>(it.key());
            if (path.endsWith(QStringLiteral(".room"), Qt::CaseInsensitive)) {
                return;
            }
        }
    }

    const QDir root(libRoot);
    if (!root.exists()) {
        return;
    }

    // Open-PDK ref style: libs.ref/<lib>/gds/<lib>.gds
    // One Cell (file basename) + one view "gds". Hierarchy lives under the expanded
    // gds node in Views — do NOT duplicate every GDS structure as a Cell.
    QString gdsPath;
    const QDir gdsDir(root.filePath(QStringLiteral("gds")));
    if (gdsDir.exists()) {
        const QStringList gdsFiles =
            gdsDir.entryList(QStringList() << QStringLiteral("*.gds") << QStringLiteral("*.GDS"),
                             QDir::Files,
                             QDir::Name);
        if (!gdsFiles.isEmpty()) {
            gdsPath = gdsDir.filePath(gdsFiles.first());
        }
    }
    if (gdsPath.isEmpty()) {
        const QStringList gdsFiles =
            root.entryList(QStringList() << QStringLiteral("*.gds") << QStringLiteral("*.GDS"),
                           QDir::Files,
                           QDir::Name);
        if (!gdsFiles.isEmpty()) {
            gdsPath = root.filePath(gdsFiles.first());
        }
    }

    if (!gdsPath.isEmpty()) {
        // Drop a previous mistaken per-structure-cell index for this PDK lib.
        const QString prefix = getLibraryKeyPrefix() + libName + QLatin1Char('/');
        QStringList toRemove;
        const QMap<QString, PropertyItem *> propItems = m_properties->getMap();
        for (auto it = propItems.constBegin(); it != propItems.constEnd(); ++it) {
            if (it.key().startsWith(prefix)) {
                toRemove << it.key();
            }
        }
        for (const QString &key : toRemove) {
            m_properties->remove(key);
        }

        const QString cellName = QFileInfo(gdsPath).completeBaseName();
        const QString key =
            getLibraryKeyPrefix() + libName + QLatin1Char('/') + cellName + QStringLiteral("/gds");
        m_properties->set(key, QDir::toNativeSeparators(gdsPath));

        // Optional layout siblings only (oas). No lef / cdl / liberty — schematic/symbol/layout.
        auto addSiblingView = [&](const QString &subdir, const QString &viewName,
                                  const QStringList &globs) {
            const QDir sub(root.filePath(subdir));
            if (!sub.exists()) {
                return;
            }
            const QStringList files = sub.entryList(globs, QDir::Files, QDir::Name);
            if (files.isEmpty()) {
                return;
            }
            const QString vkey = getLibraryKeyPrefix() + libName + QLatin1Char('/') + cellName
                + QLatin1Char('/') + viewName;
            m_properties->set(vkey, QDir::toNativeSeparators(sub.filePath(files.first())));
        };
        addSiblingView(QStringLiteral("oas"), QStringLiteral("oas"),
                       QStringList() << QStringLiteral("*.oas") << QStringLiteral("*.OAS")
                                     << QStringLiteral("*.oasis"));
        return;
    }

    // Schematic example dirs: each *.sch (and *.sym) becomes a cell.
    const QStringList schFiles = root.entryList(
        QStringList() << QStringLiteral("*.sch") << QStringLiteral("*.sym")
                      << QStringLiteral("*.SCH") << QStringLiteral("*.SYM"),
        QDir::Files,
        QDir::Name);
    for (const QString &fileName : schFiles) {
        const QFileInfo fi(root.filePath(fileName));
        const QString cell = fi.completeBaseName();
        if (cell.isEmpty()) {
            continue;
        }
        const QString view = (fi.suffix().compare(QStringLiteral("sym"), Qt::CaseInsensitive) == 0)
                                 ? QStringLiteral("sym")
                                 : QStringLiteral("sch");
        const QString key =
            getLibraryKeyPrefix() + libName + QLatin1Char('/') + cell + QLatin1Char('/') + view;
        m_properties->set(key, QDir::toNativeSeparators(fi.absoluteFilePath()));
    }
}

bool MainWindow::tryAddDirectoryAsPdk(const QString &dirPath)
{
    const QString abs = QDir::cleanPath(dirPath);
    const QFileInfo desc(QDir(abs).filePath(doorPdkDescriptorFileName()));
    const bool hasDesc = desc.exists() && desc.isFile();
    const bool looksOpenPdk = QDir(abs + QStringLiteral("/libs.ref")).exists()
        && QDir(abs + QStringLiteral("/libs.tech")).exists();
    if (!hasDesc && !looksOpenPdk) {
        return false;
    }

    DoorPdkInfo pdkInfo = loadDoorPdk(abs);
    if (!pdkInfo.ok) {
        error(tr("PDK detect failed for '%1': %2").arg(abs, pdkInfo.error), false);
        return true; // was intended as PDK; don't fall back to plain lib
    }

    if (pdkInfo.descriptorPath.isEmpty()) {
        QString writeErr;
        if (writeDoorPdkDescriptor(pdkInfo, &writeErr)) {
            pdkInfo.descriptorPath = QDir(pdkInfo.rootPath).filePath(doorPdkDescriptorFileName());
            info(tr("Wrote %1").arg(pdkInfo.descriptorPath), false);
        }
    }

    QString err;
    if (!registerLoadedPdk(pdkInfo, &err)) {
        QMessageBox::warning(this, tr("DOOR"), err);
        return true;
    }

    loadLibraries();
    applyPdkTreeGrouping();
    info(tr("PDK '%1' added (%2 libraries). Tech (agent): layers=%3 DRC=%4 LVS=%5")
             .arg(pdkInfo.name)
             .arg(pdkInfo.libraries.size())
             .arg(pdkInfo.tech.layers.isEmpty() ? tr("(none)") : pdkInfo.tech.layers)
             .arg(pdkInfo.tech.drc.isEmpty() ? tr("(none)") : pdkInfo.tech.drc)
             .arg(pdkInfo.tech.lvs.isEmpty() ? tr("(none)") : pdkInfo.tech.lvs),
         false);

    if (!m_currentProjFile.isEmpty()) {
        saveProjectFile(m_currentProjFile);
    } else {
        setStateChanged();
    }
    return true;
}

bool MainWindow::registerLoadedPdk(const DoorPdkInfo &pdk, QString *errorOut)
{
    if (!pdk.ok || pdk.rootPath.isEmpty() || pdk.name.isEmpty()) {
        if (errorOut) {
            *errorOut = pdk.error.isEmpty() ? QStringLiteral("Invalid PDK.") : pdk.error;
        }
        return false;
    }

    for (const DoorPdkInfo &existing : m_loadedPdks) {
        if (existing.name == pdk.name
            || QDir::cleanPath(existing.rootPath) == QDir::cleanPath(pdk.rootPath)) {
            if (errorOut) {
                *errorOut = tr("PDK '%1' is already in the project.").arg(pdk.name);
            }
            return false;
        }
    }

    DoorPdkInfo stored = pdk;
    QList<DoorPdkLibrary> visibleLibs;
    for (const DoorPdkLibrary &lib : pdk.libraries) {
        if (lib.name.isEmpty() || lib.path.isEmpty()) {
            continue;
        }
        if (lib.name.compare(QStringLiteral("libs.tech"), Qt::CaseInsensitive) == 0
            || lib.role.compare(QStringLiteral("tech"), Qt::CaseInsensitive) == 0) {
            continue; // agent-only via pdk.tech
        }

        if (!libraryHasIndexedViews(lib.name)) {
            setLibraryRootDirectory(lib.name, lib.path);
            indexPdkLibraryContent(lib.name, lib.path);
        }
        // If a design define() already owns this name (e.g. ROOM sg13g2_pr), keep it —
        // still show under the PDK group in the tree.
        visibleLibs.append(lib);
    }
    stored.libraries = visibleLibs;

    m_loadedPdks.append(stored);
    return true;
}

bool MainWindow::reloadLoadedPdk(const QString &pdkName, QString *errorOut)
{
    int idx = -1;
    for (int i = 0; i < m_loadedPdks.size(); ++i) {
        if (m_loadedPdks.at(i).name == pdkName) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (errorOut) {
            *errorOut = tr("PDK '%1' is not loaded.").arg(pdkName);
        }
        return false;
    }

    const DoorPdkInfo old = m_loadedPdks.at(idx);
    DoorPdkInfo fresh = loadDoorPdk(old.rootPath, pdkName);
    if (!fresh.ok) {
        if (errorOut) {
            *errorOut = fresh.error.isEmpty() ? tr("Failed to reload PDK.") : fresh.error;
        }
        return false;
    }

    // Drop property keys for libraries that disappeared; re-index new/changed roots.
    QSet<QString> newNames;
    for (const DoorPdkLibrary &lib : fresh.libraries) {
        if (lib.name.compare(QStringLiteral("libs.tech"), Qt::CaseInsensitive) == 0
            || lib.role.compare(QStringLiteral("tech"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        newNames.insert(lib.name);
    }
    for (const DoorPdkLibrary &lib : old.libraries) {
        if (!newNames.contains(lib.name)) {
            removeLibraryPropertyKeys(lib.name);
        }
    }

    QList<DoorPdkLibrary> visibleLibs;
    for (const DoorPdkLibrary &lib : fresh.libraries) {
        if (lib.name.isEmpty() || lib.path.isEmpty()) {
            continue;
        }
        if (lib.name.compare(QStringLiteral("libs.tech"), Qt::CaseInsensitive) == 0
            || lib.role.compare(QStringLiteral("tech"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        setLibraryRootDirectory(lib.name, lib.path);
        if (!libraryHasIndexedViews(lib.name)) {
            indexPdkLibraryContent(lib.name, lib.path);
        }
        visibleLibs.append(lib);
    }
    fresh.libraries = visibleLibs;
    m_loadedPdks[idx] = fresh;

    loadLibraries();
    applyPdkTreeGrouping();
    setStateChanged();
    if (!m_currentProjFile.isEmpty()) {
        saveProjectFile(m_currentProjFile);
    }
    return true;
}

void MainWindow::editPdkDescriptor(const QString &pdkName, const QString &pdkRoot)
{
    QString root = QDir::cleanPath(pdkRoot);
    if (root.isEmpty()) {
        for (const DoorPdkInfo &pdk : m_loadedPdks) {
            if (pdk.name == pdkName) {
                root = pdk.rootPath;
                break;
            }
        }
    }
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
        error(tr("PDK root not found for '%1'.").arg(pdkName), false);
        return;
    }

    QString descPath;
    for (const DoorPdkInfo &pdk : m_loadedPdks) {
        if (pdk.name == pdkName && !pdk.descriptorPath.isEmpty()) {
            descPath = pdk.descriptorPath;
            break;
        }
    }
    if (descPath.isEmpty()) {
        descPath = QDir(root).filePath(doorPdkDescriptorFileName());
    }

    if (!QFileInfo::exists(descPath)) {
        const auto reply = QMessageBox::question(
            this,
            tr("PDK Descriptor"),
            tr("No %1 found under\n%2\n\nCreate one from the current PDK layout?")
                .arg(doorPdkDescriptorFileName(), QDir::toNativeSeparators(root)),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        if (reply != QMessageBox::Yes) {
            return;
        }
        DoorPdkInfo info = loadDoorPdk(root, pdkName);
        if (!info.ok) {
            // Fall back to portable template write.
            QFile f(descPath);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                error(tr("Cannot create descriptor: %1").arg(f.errorString()), false);
                return;
            }
            f.write(openPdkTemplateJson(pdkName.isEmpty() ? QFileInfo(root).completeBaseName()
                                                          : pdkName)
                        .toUtf8());
        } else {
            QString writeErr;
            if (!writeDoorPdkDescriptor(info, &writeErr)) {
                error(tr("Cannot create descriptor: %1").arg(writeErr), false);
                return;
            }
        }
        descPath = QDir(root).filePath(doorPdkDescriptorFileName());
    }

    DoorPdkDescriptorDialog dlg(root, descPath, this);
    dlg.exec();
    if (dlg.saved()) {
        QString err;
        if (!reloadLoadedPdk(pdkName, &err)) {
            error(err, false);
        } else {
            info(tr("Reloaded PDK '%1' from descriptor.").arg(pdkName), false);
        }
    }
}

void MainWindow::applyPdkTreeGrouping()
{
    if (m_loadedPdks.isEmpty()) {
        return;
    }

    QMap<QString, QStringList> combined;
    for (const DoorPdkInfo &pdk : m_loadedPdks) {
        QStringList childNames;
        for (const DoorPdkLibrary &lib : pdk.libraries) {
            childNames << lib.name;
        }
        childNames.sort(Qt::CaseInsensitive);
        combined.insert(pdk.name, childNames);
    }
    loadCombinedLibs(combined);

    // Tag PDK parent nodes for menus / assistant.
    for (int i = 0; i < m_ui->treeLibs->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_ui->treeLibs->topLevelItem(i);
        if (!item || item->childCount() == 0) {
            continue;
        }
        for (const DoorPdkInfo &pdk : m_loadedPdks) {
            if (item->text(0) != pdk.name) {
                continue;
            }
            item->setData(0, RoleType, ItemPdkRoot);
            item->setData(0, RolePdkPath, pdk.rootPath);
            item->setToolTip(0, tr("Technology PDK\n%1").arg(pdk.rootPath));
            item->setExpanded(true);
            break;
        }
    }
}

QString MainWindow::resolvePdkRoot() const
{
    const DoorPdkInfo *pdk = activePdk();
    if (!pdk || pdk->rootPath.isEmpty()) {
        return {};
    }
    // Parent of the technology folder (…/IHP-Open-PDK for …/ihp-sg13g2).
    return QDir::fromNativeSeparators(QFileInfo(pdk->rootPath).absolutePath());
}

QString MainWindow::resolvePdkName() const
{
    const DoorPdkInfo *pdk = activePdk();
    return pdk ? pdk->name : QString();
}

QString MainWindow::resolvePdkPath() const
{
    const DoorPdkInfo *pdk = activePdk();
    return pdk ? QDir::fromNativeSeparators(pdk->rootPath) : QString();
}

QJsonObject MainWindow::buildPdkStateObject() const
{
    QJsonObject out;
    const DoorPdkInfo *pdk = activePdk();
    const QString path = pdk ? QDir::fromNativeSeparators(pdk->rootPath) : QString();
    const QString name = pdk ? pdk->name : QString();
    out.insert(QStringLiteral("pdk"), name);
    out.insert(QStringLiteral("pdk_path"), path);
    out.insert(QStringLiteral("pdk_root"), resolvePdkRoot());
    out.insert(QStringLiteral("configured"), pdk && !path.isEmpty() && QDir(path).exists());
    out.insert(QStringLiteral("descriptor"),
               pdk ? pdk->descriptorPath : QString());

    QJsonObject paths;
    if (pdk) {
        auto put = [&](const QString &key, const QString &abs) {
            if (!abs.isEmpty() && QFileInfo::exists(abs)) {
                paths.insert(key, QDir::fromNativeSeparators(abs));
            }
        };
        put(QStringLiteral("libs_tech"), pdk->tech.root);
        put(QStringLiteral("libs_tech_klayout"), pdk->tech.klayout);
        put(QStringLiteral("libs_tech_layers"), pdk->tech.layers);
        put(QStringLiteral("libs_tech_drc"), pdk->tech.drc);
        put(QStringLiteral("libs_tech_lvs"), pdk->tech.lvs);
        put(QStringLiteral("libs_tech_xschem"), pdk->tech.xschem);
        put(QStringLiteral("libs_tech_ngspice"), pdk->tech.ngspice);
        put(QStringLiteral("libs_tech_qucs"), pdk->tech.qucs);

        QJsonArray pdkLibs;
        for (const DoorPdkLibrary &lib : pdk->libraries) {
            QJsonObject lo;
            lo.insert(QStringLiteral("name"), lib.name);
            lo.insert(QStringLiteral("path"), QDir::fromNativeSeparators(lib.path));
            lo.insert(QStringLiteral("role"), lib.role);
            pdkLibs.append(lo);
            if (lib.role == QLatin1String("primitives")) {
                put(QStringLiteral("libs_ref_pr"), lib.path);
            } else if (lib.role == QLatin1String("stdcell")) {
                put(QStringLiteral("libs_ref_stdcell"), lib.path);
            } else if (lib.role == QLatin1String("io")) {
                put(QStringLiteral("libs_ref_io"), lib.path);
            }
        }
        out.insert(QStringLiteral("pdk_libraries"), pdkLibs);
    }
    out.insert(QStringLiteral("paths"), paths);

    const QString lib = getCurrentLibraryName();
    QJsonArray techLibs;
    for (const QString &t : getTechLibraryAttachList(lib)) {
        techLibs.append(t);
    }
    out.insert(QStringLiteral("tech_libraries"), techLibs);
    return out;
}

QJsonObject MainWindow::buildTechInfoObject() const
{
    QJsonObject out = buildPdkStateObject();
    out.insert(QStringLiteral("ok"), true);

    // Layer summary from attached tech ROOM libraries (compact — names only).
    QJsonArray layerSummaries;
#ifndef LIBMAN_NO_ROOM
    const QString lib = getCurrentLibraryName();
    for (const QString &techLib : getTechLibraryAttachList(lib)) {
        const QStringList roomPaths = resolveTechLibraryCorePaths(techLib);
        for (const QString &roomPath : roomPaths) {
            if (!roomPath.endsWith(QStringLiteral(".room"), Qt::CaseInsensitive)) {
                continue;
            }
            try {
                room::Database db;
                const room::RoomJsonIo::Result loaded =
                    room::RoomJsonIo::loadRoomOrJson(roomPath.toStdString(), db);
                if (!loaded.ok) {
                    continue;
                }
                QJsonObject one;
                one.insert(QStringLiteral("tech_library"), techLib);
                one.insert(QStringLiteral("room_path"), roomPath);
                one.insert(QStringLiteral("file_view"), static_cast<int>(db.fileView()));
                QJsonArray layers;
                for (const room::LayerSpec &layer : db.lib().layers()) {
                    QJsonObject lo;
                    lo.insert(QStringLiteral("name"), QString::fromStdString(layer.name));
                    lo.insert(QStringLiteral("layer"), static_cast<int>(layer.layerNum));
                    lo.insert(QStringLiteral("datatype"), static_cast<int>(layer.dataType));
                    layers.append(lo);
                    if (layers.size() >= 64) {
                        break; // keep MCP payload small
                    }
                }
                one.insert(QStringLiteral("layer_count"),
                           static_cast<int>(db.lib().layers().size()));
                one.insert(QStringLiteral("layers"), layers);
                layerSummaries.append(one);
            } catch (...) {
                // Skip unreadable tech rooms.
            }
            break; // one room per tech lib is enough for summary
        }
    }
#endif
    out.insert(QStringLiteral("tech_room_layers"), layerSummaries);
    if (const DoorPdkInfo *pdk = activePdk()) {
        QJsonObject tech;
        tech.insert(QStringLiteral("root"), pdk->tech.root);
        tech.insert(QStringLiteral("klayout"), pdk->tech.klayout);
        tech.insert(QStringLiteral("layers"), pdk->tech.layers);
        tech.insert(QStringLiteral("drc"), pdk->tech.drc);
        tech.insert(QStringLiteral("lvs"), pdk->tech.lvs);
        tech.insert(QStringLiteral("xschem"), pdk->tech.xschem);
        tech.insert(QStringLiteral("ngspice"), pdk->tech.ngspice);
        tech.insert(QStringLiteral("qucs"), pdk->tech.qucs);
        out.insert(QStringLiteral("libs_tech"), tech);
    }
    out.insert(
        QStringLiteral("note"),
        QStringLiteral(
            "Summary only. PDK comes from project pdk() + .door-pdk.json. "
            "Use libs_tech (DRC/LVS/layers) and pdk_libraries for layout work. "
            "Full GDS/ref cell dumps are intentionally omitted."));
    return out;
}

void MainWindow::registerAssistantMcpTools()
{
    if (!m_assistantMcp) {
        return;
    }

    auto emptySchema = []() {
        QJsonObject s;
        s.insert(QStringLiteral("type"), QStringLiteral("object"));
        s.insert(QStringLiteral("properties"), QJsonObject());
        return s;
    };

    m_assistantMcp->registerTool(
        QStringLiteral("list_tools"),
        QStringLiteral("List registered MCP tools and descriptions."),
        emptySchema(),
        [this](const QJsonObject &) {
            QJsonObject out;
            out.insert(QStringLiteral("ok"), true);
            out.insert(QStringLiteral("tools"), m_assistantMcp->toolsManifest());
            return out;
        });

    m_assistantMcp->registerTool(
        QStringLiteral("get_app_state"),
        QStringLiteral(
            "DOOR MCP tool (not Python): current library/cell/view/view_path. "
            "Never write room.get_app_state() — that attribute does not exist. "
            "Prefer LIVE SELECTION / run_python’s injected view_path. "
            "Never open project_file with room.open — use view_path (*.room)."),
        emptySchema(),
        [this](const QJsonObject &) {
            const QString lib = getCurrentLibraryName();
            const QString cell = getCurrentGroupName();
            const QString view = getCurrentViewName();
            const QString viewPath = getViewPath(lib, cell, view);
            QJsonObject out;
            out.insert(QStringLiteral("ok"), true);
            out.insert(QStringLiteral("project_file"), getCurrentProjectFile());
            out.insert(QStringLiteral("library"), lib);
            out.insert(QStringLiteral("cell"), cell);
            out.insert(QStringLiteral("view"), view);
            out.insert(QStringLiteral("view_path"), viewPath);
            out.insert(QStringLiteral("working_dir"), getCurrentWorkingDir());
            out.insert(QStringLiteral("schematic_symbol_format"),
                       m_properties->exists(QStringLiteral("SchematicSymbolFormat"))
                           ? m_properties->get<QString>(QStringLiteral("SchematicSymbolFormat"))
                           : QStringLiteral("ROOM"));
            const QJsonObject pdk = buildPdkStateObject();
            for (auto it = pdk.begin(); it != pdk.end(); ++it) {
                out.insert(it.key(), it.value());
            }
            return out;
        });

    m_assistantMcp->registerTool(
        QStringLiteral("get_tech_info"),
        QStringLiteral(
            "Technology / PDK summary for the current design library: PDK paths, "
            "attached tech libraries, and a compact layer table from tech ROOM files. "
            "Call when the user asks about layers, PDK, or layout generation context."),
        emptySchema(),
        [this](const QJsonObject &) { return buildTechInfoObject(); });

    {
        QJsonObject schema;
        schema.insert(QStringLiteral("type"), QStringLiteral("object"));
        QJsonObject props;
        QJsonObject cellProp;
        cellProp.insert(QStringLiteral("type"), QStringLiteral("string"));
        cellProp.insert(QStringLiteral("description"),
                        QStringLiteral("Destination cell name in the current library"));
        props.insert(QStringLiteral("target_cell"), cellProp);
        schema.insert(QStringLiteral("properties"), props);
        schema.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("target_cell")});

        m_assistantMcp->registerTool(
            QStringLiteral("copy_selected_view"),
            QStringLiteral(
                "Copy the currently selected view file into another cell of the same library "
                "(creates cell dir + registers the view). Use for “copy selected symbol/schematic "
                "to cell X”. Prefer this over inventing room.create_cell / file copies."),
            schema,
            [this](const QJsonObject &args) {
                const QString targetCell = args.value(QStringLiteral("target_cell")).toString().trimmed();
                if (targetCell.isEmpty()) {
                    return AssistantMcp::errResult(QStringLiteral("target_cell is required"));
                }
                const QString lib = getCurrentLibraryName();
                const QString srcCell = getCurrentGroupName();
                const QString view = getCurrentViewName();
                const QString srcPath = getViewPath(lib, srcCell, view);
                if (lib.isEmpty() || srcCell.isEmpty() || view.isEmpty() || srcPath.isEmpty()
                    || !QFileInfo::exists(srcPath)) {
                    return AssistantMcp::errResult(
                        QStringLiteral("No selected view to copy (lib/cell/view/path missing)"));
                }
                if (targetCell == srcCell) {
                    return AssistantMcp::errResult(
                        QStringLiteral("target_cell must differ from the selected cell"));
                }

                QString libRoot = getLibraryPath(lib);
                if (libRoot.isEmpty()) {
                    return AssistantMcp::errResult(QStringLiteral("Library root path unknown"));
                }
                const QString cellDirPath =
                    QDir::toNativeSeparators(QDir(libRoot).filePath(targetCell));
                if (!QDir().mkpath(cellDirPath)) {
                    return AssistantMcp::errResult(
                        QStringLiteral("Failed to create cell directory: %1").arg(cellDirPath));
                }

                const QString dstPath = QDir::toNativeSeparators(
                    isRoomViewName(view) ? roomViewFilePath(cellDirPath, targetCell, view)
                                         : QDir(cellDirPath).filePath(targetCell + QLatin1Char('.')
                                                                      + view));
                if (QFileInfo::exists(dstPath)) {
                    return AssistantMcp::errResult(
                        QStringLiteral("Destination already exists: %1").arg(dstPath));
                }
                if (!QFile::copy(srcPath, dstPath)) {
                    return AssistantMcp::errResult(
                        QStringLiteral("Failed to copy %1 → %2").arg(srcPath, dstPath));
                }

                // Best-effort: rewrite primary cell name inside room-json text.
                {
                    QFile f(dstPath);
                    if (f.open(QIODevice::ReadOnly)) {
                        QByteArray data = f.readAll();
                        f.close();
                        const QByteArray trimmed = data.trimmed();
                        if (trimmed.startsWith('{')) {
                            QString text = QString::fromUtf8(data);
                            text.replace(QStringLiteral("\"cell\": \"%1\"").arg(srcCell),
                                         QStringLiteral("\"cell\": \"%1\"").arg(targetCell));
                            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                                f.write(text.toUtf8());
                                f.close();
                            }
                        }
                    }
                }

                if (!registerCellViewAtPath(lib, targetCell, view, dstPath)) {
                    return AssistantMcp::errResult(
                        QStringLiteral("Copied file but failed to register view in project"));
                }

                appendPythonLog(QStringLiteral("# copy_selected_view"));
                appendPythonLog(QStringLiteral(">>> # %1 → %2").arg(srcPath, dstPath));

                QJsonObject out;
                out.insert(QStringLiteral("ok"), true);
                out.insert(QStringLiteral("src_path"), srcPath);
                out.insert(QStringLiteral("dst_path"), dstPath);
                out.insert(QStringLiteral("library"), lib);
                out.insert(QStringLiteral("src_cell"), srcCell);
                out.insert(QStringLiteral("target_cell"), targetCell);
                out.insert(QStringLiteral("view"), view);
                out.insert(QStringLiteral("note"),
                           QStringLiteral("View copied and registered. Reply with a one-line ack."));
                return out;
            });
    }

    {
        QJsonObject schema;
        schema.insert(QStringLiteral("type"), QStringLiteral("object"));
        QJsonObject props;
        QJsonObject pathProp;
        pathProp.insert(QStringLiteral("type"), QStringLiteral("string"));
        pathProp.insert(QStringLiteral("description"),
                        QStringLiteral(
                            "Absolute path to an existing *.layout.room / *.schematic.room / "
                            "*.symbol.room (or other supported view file) for the current cell. "
                            "Empty → default <cellDir>/<cell>.layout.room"));
        props.insert(QStringLiteral("path"), pathProp);
        QJsonObject viewProp;
        viewProp.insert(QStringLiteral("type"), QStringLiteral("string"));
        viewProp.insert(QStringLiteral("description"),
                        QStringLiteral("View name to register (default: layout)"));
        props.insert(QStringLiteral("view"), viewProp);
        schema.insert(QStringLiteral("properties"), props);

        m_assistantMcp->registerTool(
            QStringLiteral("register_view"),
            QStringLiteral(
                "Register an on-disk view file for the currently selected cell so it appears "
                "in DOOR’s View pane (required after run_python saves a new *.layout.room). "
                "Does not synthesize geometry — only registers an existing file."),
            schema,
            [this](const QJsonObject &args) {
                const QString lib = getCurrentLibraryName();
                const QString cell = getCurrentGroupName();
                if (lib.isEmpty() || cell.isEmpty()) {
                    return AssistantMcp::errResult(
                        QStringLiteral("No selected library/cell — select a cell first"));
                }
                QString view = args.value(QStringLiteral("view")).toString().trimmed();
                if (view.isEmpty()) {
                    view = QStringLiteral("layout");
                }
                QString path = args.value(QStringLiteral("path")).toString().trimmed();
                if (path.isEmpty()) {
                    const QString libRoot = getLibraryPath(lib);
                    if (libRoot.isEmpty()) {
                        return AssistantMcp::errResult(QStringLiteral("Library root path unknown"));
                    }
                    const QString cellDir = QDir(libRoot).filePath(cell);
                    path = isRoomViewName(view) ? roomViewFilePath(cellDir, cell, view)
                                                : QDir(cellDir).filePath(cell + QLatin1Char('.') + view);
                }
                path = QDir::toNativeSeparators(path);
                if (!QFileInfo::exists(path)) {
                    return AssistantMcp::errResult(
                        QStringLiteral("File does not exist (create it first): %1").arg(path));
                }
                if (!registerCellViewAtPath(lib, cell, view, path)) {
                    return AssistantMcp::errResult(
                        QStringLiteral("Failed to register view '%1' at %2").arg(view, path));
                }
                appendPythonLog(QStringLiteral("# register_view"));
                appendPythonLog(QStringLiteral(">>> # %1 → %2/%3/%4").arg(path, lib, cell, view));
                QJsonObject out;
                out.insert(QStringLiteral("ok"), true);
                out.insert(QStringLiteral("path"), path);
                out.insert(QStringLiteral("library"), lib);
                out.insert(QStringLiteral("cell"), cell);
                out.insert(QStringLiteral("view"), view);
                out.insert(QStringLiteral("note"),
                           QStringLiteral(
                               "View registered in DOOR. Reply that layout/view is now listed — "
                               "only if this path is a real layout file you created."));
                return out;
            });
    }

    {
        QJsonObject schema;
        schema.insert(QStringLiteral("type"), QStringLiteral("object"));
        QJsonObject props;
        QJsonObject codeProp;
        codeProp.insert(QStringLiteral("type"), QStringLiteral("string"));
        props.insert(QStringLiteral("code"), codeProp);
        schema.insert(QStringLiteral("properties"), props);
        schema.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("code")});

        m_assistantMcp->registerTool(
            QStringLiteral("run_python"),
            QStringLiteral(
                "Run Python in DOOR Command Line (ROOM preloaded). "
                "DOOR injects view_path/library/cell/view from the current selection — "
                "do NOT call room.get_app_state (it does not exist). "
                "Code and stdout/stderr appear only in Messages. "
                "Use print(...) for results. Returns ok/error only — not the printed data."),
            schema,
            [this](const QJsonObject &args) {
                const QString code = args.value(QStringLiteral("code")).toString();
                if (code.trimmed().isEmpty()) {
                    return AssistantMcp::errResult(QStringLiteral("code is required"));
                }
                if (!m_pythonConsole) {
                    return AssistantMcp::errResult(QStringLiteral("Python console not available"));
                }
                if (!m_pythonConsole->isRunning()) {
                    m_pythonConsole->start();
                }
                if (m_pythonConsole->isBusy()) {
                    return AssistantMcp::errResult(
                        QStringLiteral("Command Line is busy — wait and retry."));
                }

                // Fresh selection for this exec — agents often invent room.get_app_state().
                const QString lib = getCurrentLibraryName();
                const QString cell = getCurrentGroupName();
                const QString view = getCurrentViewName();
                const QString viewPath = getViewPath(lib, cell, view);
                const QString pdkRoot = resolvePdkRoot();
                const QString pdk = resolvePdkName();
                const QString pdkPath = resolvePdkPath();
                auto pyStr = [](const QString &s) {
                    QString t = s;
                    t.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
                    t.replace(QLatin1Char('\''), QStringLiteral("\\'"));
                    return QStringLiteral("'") + t + QStringLiteral("'");
                };
                // Bind selection + PDK vars and a compatibility room.get_app_state() shim
                // (agents invent that name; real get_app_state is a DOOR MCP tool).
                const QString preamble =
                    QStringLiteral(
                        "view_path = %1\n"
                        "library = %2\n"
                        "cell = %3\n"
                        "view = %4\n"
                        "pdk_root = %5\n"
                        "pdk = %6\n"
                        "pdk_path = %7\n"
                        "import room as _door_room\n"
                        "def _door_get_app_state():\n"
                        "    return {'library': library, 'cell': cell, 'view': view, "
                        "'view_path': view_path, 'pdk_root': pdk_root, 'pdk': pdk, "
                        "'pdk_path': pdk_path}\n"
                        "_door_room.get_app_state = _door_get_app_state\n")
                        .arg(pyStr(viewPath), pyStr(lib), pyStr(cell), pyStr(view),
                             pyStr(pdkRoot), pyStr(pdk), pyStr(pdkPath));
                const QString fullCode = preamble + code;

                // Echo like Virtuoso CIW / Command Line (user code only; skip injected prelude).
                const QStringList lines = code.split(QLatin1Char('\n'));
                for (int i = 0; i < lines.size(); ++i) {
                    appendPythonLog((i == 0 ? QStringLiteral(">>> ") : QStringLiteral("... "))
                                    + lines.at(i));
                }

                bool finishedOk = false;
                bool gotFinished = false;
                QEventLoop loop;
                QTimer timeout;
                timeout.setSingleShot(true);
                QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
                const QMetaObject::Connection finishedConn = QObject::connect(
                    m_pythonConsole, &PythonConsole::commandFinished, &loop,
                    [&](bool ok) {
                        finishedOk = ok;
                        gotFinished = true;
                        loop.quit();
                    });
                timeout.start(60000);
                m_pythonConsole->execute(fullCode);
                loop.exec();
                QObject::disconnect(finishedConn);

                if (!gotFinished) {
                    return AssistantMcp::errResult(QStringLiteral("Command Line timed out"));
                }
                QJsonObject out;
                out.insert(QStringLiteral("ok"), finishedOk);
                out.insert(QStringLiteral("note"),
                           QStringLiteral(
                               "Output is in Messages only. Reply with a one-line ack; "
                               "do not repeat printed data in chat."));
                if (!finishedOk) {
                    out.insert(QStringLiteral("error"),
                               QStringLiteral("Python reported an error — see Messages."));
                }
                return out;
            });
    }

    // Room MCP tools (available; prefer run_python so results stay in Messages).
    auto registerRoomProxy = [this](const QString &name, const QString &description,
                                    const QJsonObject &schema) {
        m_assistantMcp->registerTool(
            name, description, schema,
            [this, name](const QJsonObject &args) {
                if (!m_roomMcp) {
                    return AssistantMcp::errResult(QStringLiteral("Room MCP client missing"));
                }
                // Do not mirror MCP payloads into Messages — that duplicates Command Line.
                return m_roomMcp->callTool(name, args);
            });
    };

    {
        QJsonObject pathSchema;
        pathSchema.insert(QStringLiteral("type"), QStringLiteral("object"));
        QJsonObject props;
        QJsonObject p;
        p.insert(QStringLiteral("type"), QStringLiteral("string"));
        props.insert(QStringLiteral("path"), p);
        pathSchema.insert(QStringLiteral("properties"), props);
        pathSchema.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("path")});

        registerRoomProxy(QStringLiteral("room_info"),
                          QStringLiteral("ROOM file info via Room MCP (Python API)."),
                          pathSchema);
        registerRoomProxy(QStringLiteral("room_sniff"),
                          QStringLiteral("Fast ROOM header sniff via Room MCP."),
                          pathSchema);
        registerRoomProxy(QStringLiteral("room_ls"),
                          QStringLiteral(
                              "List cell (device) names — and optionally layers — in a .room file."),
                          pathSchema);
    }
    {
        QJsonObject schema;
        schema.insert(QStringLiteral("type"), QStringLiteral("object"));
        QJsonObject props;
        QJsonObject code;
        code.insert(QStringLiteral("type"), QStringLiteral("string"));
        props.insert(QStringLiteral("code"), code);
        schema.insert(QStringLiteral("properties"), props);
        schema.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("code")});
        registerRoomProxy(
            QStringLiteral("room_python_eval"),
            QStringLiteral(
                "Full ROOM Python API (same as import room). Prefer run_python for "
                "Messages echo; use this when a returned result value is needed."),
            schema);
        registerRoomProxy(
            QStringLiteral("room_api_help"),
            QStringLiteral("Cheat-sheet of the full ROOM Python API (Database/Cell/CellContent)."),
            emptySchema());
        {
            QJsonObject schema;
            schema.insert(QStringLiteral("type"), QStringLiteral("object"));
            QJsonObject props;
            QJsonObject obj;
            obj.insert(QStringLiteral("type"), QStringLiteral("string"));
            props.insert(QStringLiteral("object"), obj);
            schema.insert(QStringLiteral("properties"), props);
            schema.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("object")});
            registerRoomProxy(
                QStringLiteral("room_api_dir"),
                QStringLiteral("Introspect room/Database/Cell/CellContent (dir + signatures)."),
                schema);
        }
    }
    {
        QJsonObject schema;
        schema.insert(QStringLiteral("type"), QStringLiteral("object"));
        QJsonObject props;
        for (const char *k : {"source", "dest", "view", "format", "tool", "cell"}) {
            QJsonObject p;
            p.insert(QStringLiteral("type"), QStringLiteral("string"));
            props.insert(QString::fromLatin1(k), p);
        }
        schema.insert(QStringLiteral("properties"), props);
        schema.insert(QStringLiteral("required"),
                      QJsonArray{QStringLiteral("source"), QStringLiteral("dest")});
        registerRoomProxy(QStringLiteral("room_convert"),
                          QStringLiteral("Convert GDS/OAS/Qucs/Xschem ↔ ROOM via Room MCP."),
                          schema);
    }
}

void MainWindow::setupPythonConsole()
{
    if (!m_ui || !m_ui->editPythonCommand) {
        return;
    }
    QFont mono = m_ui->editPythonCommand->font();
    mono.setFamily(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(qMax(9.0, mono.pointSizeF()));
    m_ui->editPythonCommand->setFont(mono);
    m_ui->editPythonCommand->installEventFilter(this);
    m_ui->editPythonCommand->setTabChangesFocus(false);

#ifdef LIBMAN_TESTING
    // GUI unit tests construct MainWindow without a stable Python/ROOM env;
    // spawning the REPL subprocess aborts CI. Skip the process; keep the UI wired.
    return;
#else
    m_pythonConsole = new PythonConsole(this);
    connect(m_pythonConsole, &PythonConsole::started, this, &MainWindow::onPythonConsoleStarted);
    connect(m_pythonConsole, &PythonConsole::failed, this, &MainWindow::onPythonConsoleFailed);
    connect(m_pythonConsole, &PythonConsole::output, this, &MainWindow::onPythonConsoleOutput);
    connect(m_pythonConsole, &PythonConsole::commandFinished, this, &MainWindow::onPythonCommandFinished);
    connect(m_pythonConsole, &PythonConsole::libmanCommand, this, &MainWindow::onPythonLibmanCommand);

    // Auto-start so `import room` is ready when DOOR opens (Virtuoso CIW style).
    QTimer::singleShot(0, m_pythonConsole, &PythonConsole::start);
#endif
}

void MainWindow::appendPythonLog(const QString &text, bool isError)
{
    if (text.isEmpty()) {
        return;
    }
    QTextCursor cursor = logCursorAtEnd(m_ui->textMessages, false);
    QTextCharFormat fmt = logPlainCharFormat();
    if (isError) {
        fmt.setForeground(QBrush(Qt::red));
    }
    QString body = text;
    if (!body.endsWith(QLatin1Char('\n'))) {
        body += QLatin1Char('\n');
    }
    cursor.insertText(body, fmt);
    cursor.setCharFormat(logPlainCharFormat());
    m_ui->textMessages->setTextCursor(cursor);
}

void MainWindow::pushPythonHistory(const QString &code)
{
    const QString trimmed = code.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (!m_pythonHistory.isEmpty() && m_pythonHistory.last() == trimmed) {
        m_pythonHistoryIndex = -1;
        m_pythonHistoryDraft.clear();
        return;
    }
    m_pythonHistory.append(trimmed);
    constexpr int kMaxHistory = 200;
    while (m_pythonHistory.size() > kMaxHistory) {
        m_pythonHistory.removeFirst();
    }
    m_pythonHistoryIndex = -1;
    m_pythonHistoryDraft.clear();
}

bool MainWindow::recallPythonHistory(int delta)
{
    if (m_pythonHistory.isEmpty() || delta == 0) {
        return false;
    }

    if (m_pythonHistoryIndex < 0) {
        // Start browsing from just past the newest entry.
        m_pythonHistoryDraft = m_ui->editPythonCommand->toPlainText();
        m_pythonHistoryIndex = m_pythonHistory.size();
    }

    const int next = m_pythonHistoryIndex + delta;
    if (next < 0) {
        return true; // stay on oldest
    }
    if (next >= m_pythonHistory.size()) {
        m_pythonHistoryIndex = -1;
        m_ui->editPythonCommand->setPlainText(m_pythonHistoryDraft);
        QTextCursor cursor = m_ui->editPythonCommand->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_ui->editPythonCommand->setTextCursor(cursor);
        return true;
    }

    m_pythonHistoryIndex = next;
    m_ui->editPythonCommand->setPlainText(m_pythonHistory.at(m_pythonHistoryIndex));
    QTextCursor cursor = m_ui->editPythonCommand->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_ui->editPythonCommand->setTextCursor(cursor);
    return true;
}

void MainWindow::runPythonCommand()
{
    if (!m_pythonConsole) {
        return;
    }

    QString code = m_ui->editPythonCommand->textCursor().hasSelection()
                       ? m_ui->editPythonCommand->textCursor().selectedText().replace(QChar(0x2029), QLatin1Char('\n'))
                       : m_ui->editPythonCommand->toPlainText();
    code = code.trimmed();
    if (code.isEmpty()) {
        return;
    }

    pushPythonHistory(code);

    // Echo like Virtuoso CIW.
    QStringList lines = code.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        appendPythonLog((i == 0 ? QStringLiteral(">>> ") : QStringLiteral("... ")) + lines.at(i));
    }

    m_ui->editPythonCommand->setEnabled(false);
    m_pythonConsole->execute(code);
    if (!m_ui->editPythonCommand->textCursor().hasSelection()) {
        m_ui->editPythonCommand->clear();
    }
}

void MainWindow::onPythonConsoleOutput(const QString &text)
{
    const QString trimmed = text;
    if (trimmed.contains(QStringLiteral("WARNING:"), Qt::CaseSensitive)
        || trimmed.contains(QStringLiteral("Error"), Qt::CaseInsensitive)
        || trimmed.contains(QStringLiteral("Traceback"), Qt::CaseSensitive)) {
        appendPythonLog(trimmed, true);
    } else {
        appendPythonLog(trimmed, false);
    }
}

void MainWindow::onPythonConsoleStarted(const QString &detail)
{
    Q_UNUSED(detail);
    // Keep startup quiet — ROOM ready line comes from the interpreter itself.
}

void MainWindow::onPythonConsoleFailed(const QString &reason)
{
    error(reason, false);
    m_ui->editPythonCommand->setEnabled(true);
}

void MainWindow::onPythonCommandFinished(bool)
{
    m_ui->editPythonCommand->setEnabled(true);
    m_ui->editPythonCommand->setFocus(Qt::OtherFocusReason);
}

void MainWindow::onPythonLibmanCommand(const QJsonObject &cmd)
{
    const QString op = cmd.value(QStringLiteral("cmd")).toString().trimmed().toLower();
    const QString lib = cmd.value(QStringLiteral("library")).toString().trimmed();
    const QString path = cmd.value(QStringLiteral("path")).toString().trimmed();
    if(op.isEmpty() || lib.isEmpty() || path.isEmpty()) {
        error(tr("CML command missing cmd/library/path."), false);
        return;
    }

    if(op == QLatin1String("define")) {
        if(!registerViewFromCml(lib, path)) {
            error(tr("define() failed for '%1' / '%2'.").arg(lib, path), false);
        }
        return;
    }
    if(op == QLatin1String("undefine")) {
        if(!unregisterViewFromCml(lib, path)) {
            error(tr("undefine() failed for '%1' / '%2'.").arg(lib, path), false);
        }
        return;
    }
    error(tr("Unknown CML command '%1'.").arg(op), false);
}

void MainWindow::saveProjectFileQuiet(const QString &fileName)
{
    if(fileName.isEmpty()) {
        return;
    }
    m_ignoreProjectFileChange = true;
    if(!saveProjectEntriesToFile(fileName, getCurrentProjectEntries())) {
        QTimer::singleShot(100, this, [this]() {
            m_ignoreProjectFileChange = false;
        });
        return;
    }
    setupProjectFileWatcher(QFileInfo(fileName).absoluteFilePath());
    setStateSaved();
    QTimer::singleShot(100, this, [this]() {
        m_ignoreProjectFileChange = false;
    });
}

void MainWindow::ensureLibraryInTree(const QString &libName)
{
    if(libName.isEmpty() || !m_ui || !m_ui->treeLibs) {
        return;
    }
    for(int i = 0; i < m_ui->treeLibs->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_ui->treeLibs->topLevelItem(i);
        if(item && item->text(0) == libName) {
            return;
        }
    }
    QTreeWidgetItem *item = new QTreeWidgetItem;
    item->setText(0, libName);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    m_ui->treeLibs->addTopLevelItem(item);
#if QT_VERSION >= 0x050000
    m_ui->treeLibs->sortByColumn(0, Qt::AscendingOrder);
#else
    m_ui->treeLibs->sortByColumn(0);
#endif
}

void MainWindow::ensureGroupInList(const QString &groupName)
{
    if(groupName.isEmpty() || !m_ui || !m_ui->listGroups) {
        return;
    }
    for(int i = 0; i < m_ui->listGroups->count(); ++i) {
        QListWidgetItem *item = m_ui->listGroups->item(i);
        if(item && item->text() == groupName) {
            return;
        }
    }
    QListWidgetItem *groupItem = new QListWidgetItem;
    groupItem->setText(groupName);
    groupItem->setFlags(groupItem->flags() | Qt::ItemIsEditable);
    m_ui->listGroups->addItem(groupItem);
    m_ui->listGroups->sortItems();
}

void MainWindow::ensureViewInList(const QString &libName,
                                  const QString &groupName,
                                  const QString &viewName,
                                  const QString &viewPath)
{
    if(!m_ui || !m_ui->listViews) {
        return;
    }
    if(getCurrentLibraryName() != libName || getCurrentGroupName() != groupName) {
        return;
    }
    for(int i = 0; i < m_ui->listViews->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_ui->listViews->topLevelItem(i);
        if(item && item->text(0).trimmed() == viewName) {
            return;
        }
    }

    QTreeWidgetItem *viewItem = new QTreeWidgetItem(m_ui->listViews);
    viewItem->setText(0, viewName);
    if(viewName == QLatin1String("gds")) {
        viewItem->setData(0, RoleType, ItemViewGds);
        viewItem->setData(0, RoleGdsPath, viewPath);
        applyViewTreeIcon(viewItem, viewName);
        viewItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    } else if(viewName == QLatin1String("oas") || viewName == QLatin1String("oasis")) {
        viewItem->setData(0, RoleType, ItemViewOas);
        viewItem->setData(0, RoleOasPath, viewPath);
        applyViewTreeIcon(viewItem, viewName);
        viewItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    } else if(viewName == QLatin1String("lstr")) {
        viewItem->setData(0, RoleType, ItemViewLStream);
        viewItem->setData(0, RoleLStreamPath, viewPath);
        applyViewTreeIcon(viewItem, viewName);
        viewItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    } else if(isEmSetupViewName(viewName)) {
        configureEmSetupViewTreeItem(viewItem, viewPath);
    } else if(isRoomViewName(viewName)) {
        configureCoreViewTreeItem(viewItem, viewName, viewPath);
        applyCoreViewLockPresentation(viewItem, viewName, viewPath);
    } else {
        applyViewTreeIcon(viewItem, viewName);
    }
    m_ui->listViews->sortItems(0, Qt::AscendingOrder);
    syncCoreLockWatches();
}

bool MainWindow::registerViewFromCml(const QString &libName, const QString &viewPath)
{
    if(m_currentProjFile.isEmpty()) {
        error(tr("No project file open — cannot define()."), false);
        return false;
    }

    const QString absPath = QFileInfo(viewPath).absoluteFilePath();
    if(!QFileInfo::exists(absPath)) {
        error(tr("View file does not exist: %1").arg(absPath), false);
        return false;
    }

    QString groupName;
    QString viewName;
    if(!resolveCellViewFromPath(absPath, &groupName, &viewName) || groupName.isEmpty() || viewName.isEmpty()) {
        error(tr("Cannot derive cell/view from path: %1").arg(absPath), false);
        return false;
    }

    const QString key = getLibraryKeyPrefix() + libName + "/" + groupName + "/" + viewName;
    m_properties->set(key, absPath);

    ensureLibraryInTree(libName);
    if(getCurrentLibraryName() == libName) {
        ensureGroupInList(groupName);
        ensureViewInList(libName, groupName, viewName, absPath);
    }

    saveProjectFileQuiet(m_currentProjFile);
    info(tr("Registered %1 / %2 / %3").arg(libName, groupName, viewName), false);
    return true;
}

bool MainWindow::unregisterViewFromCml(const QString &libName, const QString &viewPath)
{
    if(m_currentProjFile.isEmpty()) {
        error(tr("No project file open — cannot undefine()."), false);
        return false;
    }

    const QString absPath = QFileInfo(viewPath).absoluteFilePath();
    QString groupName;
    QString viewName;
    if(!resolveCellViewFromPath(absPath, &groupName, &viewName) || groupName.isEmpty() || viewName.isEmpty()) {
        error(tr("Cannot derive cell/view from path: %1").arg(absPath), false);
        return false;
    }

    const QString key = getLibraryKeyPrefix() + libName + "/" + groupName + "/" + viewName;
    if(m_properties->exists(key)) {
        m_properties->remove(key);
    }

    if(getCurrentLibraryName() == libName) {
        if(getCurrentGroupName() == groupName && m_ui && m_ui->listViews) {
            for(int idx = 0; idx < m_ui->listViews->topLevelItemCount(); ++idx) {
                QTreeWidgetItem *viewItem = m_ui->listViews->topLevelItem(idx);
                if(viewItem && viewItem->text(0).trimmed() == viewName) {
                    delete m_ui->listViews->takeTopLevelItem(idx);
                    break;
                }
            }
        }
        // Drop empty cell from the group list when no views remain.
        if(getCurrentViews(libName, groupName).isEmpty() && m_ui && m_ui->listGroups) {
            for(int i = 0; i < m_ui->listGroups->count(); ++i) {
                QListWidgetItem *item = m_ui->listGroups->item(i);
                if(item && item->text() == groupName) {
                    delete m_ui->listGroups->takeItem(i);
                    m_ui->listViews->clear();
                    break;
                }
            }
        }
    }

    saveProjectFileQuiet(m_currentProjFile);
    info(tr("Unregistered %1 / %2 / %3").arg(libName, groupName, viewName), false);
    return true;
}

QString MainWindow::symbolCoreForCell(const QString &cellName) const
{
    if (cellName.isEmpty() || m_properties == nullptr) {
        return QString();
    }

    QString libraryHint;
    QString bareCell = cellName.trimmed();
    bareCell.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const int slash = bareCell.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0) {
        libraryHint = bareCell.left(slash);
        bareCell = bareCell.mid(slash + 1);
    }
    const QStringList viewSuffixes = {
        QStringLiteral(".symbol.room"),
        QStringLiteral(".sym"),
        QStringLiteral(".symbol")
    };
    for (const QString &suffix : viewSuffixes) {
        if (bareCell.endsWith(suffix, Qt::CaseInsensitive)) {
            bareCell.chop(suffix.size());
            break;
        }
    }
    if (bareCell.isEmpty()) {
        return QString();
    }

    const auto existingFile = [](const QString &path) {
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            return QString();
        }
        return QFileInfo(path).absoluteFilePath();
    };

    const auto symbolInLibrary = [&](const QString &libraryName) {
        if (libraryName.isEmpty()) {
            return QString();
        }

        const QString key = getLibraryKeyPrefix() + libraryName + QLatin1Char('/') + bareCell + QStringLiteral("/symbol");
        if (m_properties->exists(key)) {
            const QString registered = existingFile(m_properties->get<QString>(key).trimmed());
            if (!registered.isEmpty()) {
                return registered;
            }
        }

        const QString libraryRoot = getLibraryPath(libraryName);
        if (!libraryRoot.isEmpty()) {
            const QString onDisk = existingFile(QDir(libraryRoot).filePath(
                bareCell + QLatin1Char('/') + bareCell + QStringLiteral(".symbol.room")));
            if (!onDisk.isEmpty()) {
                return onDisk;
            }
        }
        return QString();
    };

    const QString currentLibrary = getCurrentLibraryName();
    if (!libraryHint.isEmpty()) {
        const QString hinted = symbolInLibrary(libraryHint);
        if (!hinted.isEmpty()) {
            return hinted;
        }
    }

    const QString inDesign = symbolInLibrary(currentLibrary);
    if (!inDesign.isEmpty()) {
        return inDesign;
    }

    for (const QString &tech : getTechLibraryAttachList(currentLibrary)) {
        const QString attached = symbolInLibrary(tech);
        if (!attached.isEmpty()) {
            return attached;
        }
    }

    return QString();
}

void MainWindow::refreshSnapshot()
{
    if (m_snapshotView == nullptr || !m_ui->groupSnapshot->isVisible()) {
        return;
    }

    const QList<QTreeWidgetItem *> selected = m_ui->listViews->selectedItems();
    if (selected.isEmpty()) {
        m_snapshotView->clearScene(tr("Select a schematic, symbol, layout, or emmodel"));
        return;
    }

    QTreeWidgetItem *item = selected.first();

    QTreeWidgetItem *root = item;
    while (root->parent() != nullptr) {
        root = root->parent();
    }

    const QString viewName = root->text(0).trimmed();
    const QString view = viewName.toLower();
    const bool isLayout = isLayoutRoomViewName(view);
    const bool isEmModel = (view == QLatin1String("emmodel"));
    const bool isEmSetup = isEmSetupViewName(view);
    if (view != QLatin1String("schematic") && view != QLatin1String("symbol")
        && view != QLatin1String("sch") && view != QLatin1String("sym")
        && !isLayout && !isEmModel && !isEmSetup) {
        m_snapshotView->clearScene(
            tr("Snapshot is available for schematic, symbol, layout, and emmodel (ROOM layout)"));
        return;
    }

    QString viewPath;
    if (root->data(0, RoleType).toInt() == ItemViewCore) {
        viewPath = root->data(0, RoleCorePath).toString();
    } else if (root->data(0, RoleType).toInt() == ItemViewEmSetup) {
        viewPath = root->data(0, RoleEmSetupPath).toString();
    } else {
        viewPath = getViewPath(getCurrentLibraryName(), getCurrentGroupName(), viewName);
    }

    const auto symbolResolver = [this](const QString &name) {
        return symbolCoreForCell(name);
    };

    // emmodel: preview the frozen ROOM layout from topology.layoutPath (layout_room).
    if (isEmModel) {
        if (viewPath.isEmpty() || !QFileInfo::exists(viewPath)) {
            m_snapshotView->clearScene(tr("No file for this view"));
            return;
        }
        QString topCell;
        const QString layoutRoom = layoutRoomPathFromEmModel(viewPath, &topCell);
        if (layoutRoom.isEmpty()) {
            m_snapshotView->clearScene(
                tr("No ROOM layout in this emmodel\n"
                   "(topology.layoutPath must be *.layout.room)"));
            return;
        }
        if (topCell.isEmpty()) {
            topCell = getCurrentGroupName();
        }
        m_snapshotView->setScene(
            loadCoreSnapshot(layoutRoom, QStringLiteral("layout"), symbolResolver, topCell));
        return;
    }

    // emsetup: show the cell's layout.room when present (same cell, ROOM only).
    if (isEmSetup) {
        const QString layoutPath =
            getViewPath(getCurrentLibraryName(), getCurrentGroupName(), QStringLiteral("layout"));
        if (layoutPath.isEmpty() || !QFileInfo::exists(layoutPath)) {
            m_snapshotView->clearScene(tr("No ROOM layout view for this cell"));
            return;
        }
        m_snapshotView->setScene(loadCoreSnapshot(
            layoutPath, QStringLiteral("layout"), symbolResolver, getCurrentGroupName()));
        return;
    }

    if (viewPath.isEmpty() || !QFileInfo::exists(viewPath)) {
        m_snapshotView->clearScene(tr("No file for this view"));
        return;
    }

    QString focusCell;
    if (item->data(0, RoleType).toInt() == ItemCell) {
        focusCell = item->data(0, RoleCellName).toString();
    } else if (isLayout) {
        focusCell = getCurrentGroupName();
    }

    m_snapshotView->setScene(loadCoreSnapshot(viewPath, view, symbolResolver, focusCell));
}

/*!*******************************************************************************************************************
 * \brief Generates unexisting name based on the given information. Used when coping an item.
 * \param name       Name of the item (project, cell, view).
 * \param path       Path to the folder where the item (project, cell, view) exists.
 * \param suffix     Suffix of the output file (used for view name generation).
 **********************************************************************************************************************/
QString MainWindow::generateCopyName(const QString &name, const QString &path, const QString &suffix) const
{
    QString copyName = name + "_copy";

    QString copyPath = QDir::toNativeSeparators(path + "/" + copyName + suffix);
    if(!QFileInfo(copyPath).exists()) {
        return copyPath;
    }

    int index = 1;
    bool nameExists = true;
    while(nameExists) {
        copyName = name + "_copy" + QString("%1").arg(index);
        copyPath = QDir::toNativeSeparators(path + "/" + copyName + suffix);
        if(!QFileInfo(copyPath).exists()) {
            break;
        }

        index++;
    }

    return copyPath;
}

/*!*******************************************************************************************************************
 * \brief Slot is triggered when tree item is changed. Used for storing the project names after renaming.
 * \param item       Pointer to item which has been changed.
 * \param column     Column where item which has been changed.
 **********************************************************************************************************************/
void MainWindow::on_treeLibs_itemChanged(QTreeWidgetItem *item, int column)
{
    if(!item) {
        return;
    }

    if(column != 0) {
        return;
    }

    if(item->childCount()) {
        return;
    }

    if(m_itemText.isEmpty()) {
        return;
    }

    QString projName = item->text(column);
    QString key = getLibraryKeyPrefix() + m_itemText;
    QString libPath = m_properties->get<QString> (key);

    if(!QFileInfo(libPath).isDir()) {
        return;
    }

    QString targetPath = QDir::toNativeSeparators(QFileInfo(libPath).absolutePath() + "/" + projName);
    if(QFileInfo(targetPath).exists()) {
        error(QString("Directory '%1' aleardy exists.").arg(targetPath));
        item->setText(column, m_itemText);
        return;
    }

    QDir dir;
    dir.rename(libPath, targetPath);

    m_properties->remove(key);

    key = getLibraryKeyPrefix() + projName;
    m_properties->set(key, targetPath);

    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Returns pointer to tree item.
 * \param name       Name of the item to look for.
 **********************************************************************************************************************/
QTreeWidgetItem* MainWindow::getTreeItemByName(const QString &name)
{
    for(int i = 0; i < m_ui->treeLibs->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_ui->treeLibs->topLevelItem(i);
        if(!item) {
            continue;
        }

        if(item->text(0) == name) {
            return item;
        }

        for(int j = 0; j < item->childCount(); ++j) {
            QTreeWidgetItem *child = item->child(j);
            if(!child) {
                continue;
            }

            if(child->text(0) == name) {
                return child;
            }
        }
    }

    return 0;
}

/*!*******************************************************************************************************************
 * \brief Searches for *.lib files in the specified directory and returns the first valid one.
 * \param dirName     Name of folder to search for lib file.
 **********************************************************************************************************************/
QString MainWindow::getProjectFileFromDir(const QString &dirName) const
{
    QString projFile;

    QStringList formats;
    formats << "*.lib";

    QDir projDir(dirName);
    projDir.setNameFilters(formats);

    QStringList fileList = projDir.entryList();
    foreach(QString projName, fileList) {
        QString projPath = QDir::toNativeSeparators(dirName + "/" + projName);
        if(!QFileInfo(projPath).isFile()) {
            continue;
        }

        LibFileParser parser;
        if(parser.parseFile(projPath)) {
            return projPath;
        }
    }

    return projFile;
}

/*!*******************************************************************************************************************
 * \brief Hides tree item during filtering.
 * \param tree       Tree pointer used for filtering.
 * \param filter     Text string used to filter tree items.
 **********************************************************************************************************************/
void MainWindow::hideTreeItem(QTreeWidget *tree, const QString &filter)
{
    if(!tree) {
        return;
    }

    for(int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        if(!item) {
            continue;
        }

        if(filter.isEmpty()) {
            item->setHidden(false);
            continue;
        }

        item->setHidden(item->text(0).contains(filter) ? false : true);
    }
}

/*!*******************************************************************************************************************
 * \brief Hides list item during filtering.
 * \param list       List pointer used for filtering.
 * \param filter     Text string used to filter list items.
 **********************************************************************************************************************/
void MainWindow::hideListItem(QListWidget *list, const QString &filter)
{
    if(!list) {
        return;
    }

    for(int i = 0; i < list->count(); ++i) {
        QListWidgetItem *item = list->item(i);
        if(!item) {
            continue;
        }

        if(filter.isEmpty()) {
            item->setHidden(false);
            continue;
        }

        item->setHidden(item->text().contains(filter) ? false : true);
    }
}

/*!*******************************************************************************************************************
 * \brief Recursively hides or shows tree items based on filter string.
 * \param item       Tree item to process.
 * \param filter     Text string used to filter items.
 * \return true if item or any of its children matches the filter.
 **********************************************************************************************************************/
bool MainWindow::filterTreeItem(QTreeWidgetItem *item, const QString &filter)
{
    if (!item) {
        return false;
    }

    bool match = item->text(0).contains(filter, Qt::CaseInsensitive);

    bool childMatch = false;
    for (int i = 0; i < item->childCount(); ++i) {
        if (filterTreeItem(item->child(i), filter)) {
            childMatch = true;
        }
    }

    bool visible = filter.isEmpty() || match || childMatch;
    item->setHidden(!visible);

    if (childMatch && !filter.isEmpty()) {
        item->setExpanded(true);
    }

    return match || childMatch;
}

/*!*******************************************************************************************************************
 * \brief Filters projects (libraries) based on user input.
 * \param filter     Text string used to filter projects (libraries).
 **********************************************************************************************************************/
void MainWindow::on_txtLibSearch_textEdited(const QString &filter)
{
    hideTreeItem(m_ui->treeLibs, filter);
}

/*!*******************************************************************************************************************
 * \brief Filters categories based on user input.
 * \param filter     Text string used to filter categories.
 **********************************************************************************************************************/
void MainWindow::on_txtCatSearch_textEdited(const QString &filter)
{
    hideTreeItem(m_ui->listCategories, filter);
}

/*!*******************************************************************************************************************
 * \brief Filters groups (cells) based on user input.
 * \param filter     Text string used to filter groups (cells).
 **********************************************************************************************************************/
void MainWindow::on_txtCellSearch_textEdited(const QString &filter)
{
    hideListItem(m_ui->listGroups, filter);
}

/*!*******************************************************************************************************************
 * \brief Triggers execution of About dialog window.
 **********************************************************************************************************************/
void MainWindow::on_actionAbout_triggered()
{
    About(this).exec();
}

/*!*******************************************************************************************************************
 * \brief Triggers creation of new project (library).
 **********************************************************************************************************************/
void MainWindow::on_actionProject_triggered()
{
    addNewProject();
}

/*!*******************************************************************************************************************
 * \brief Triggers creation of new group (cell).
 **********************************************************************************************************************/
void MainWindow::on_actionGroup_triggered()
{
    addNewGroup();
}

/*!*******************************************************************************************************************
 * \brief Triggers execution of NewView dialog window.
 **********************************************************************************************************************/
void MainWindow::on_actionUnion_triggered()
{
    QString libName = getCurrentLibraryName();
    if(libName.isEmpty()) {
        return;
    }

    QString groupName = getCurrentGroupName();
    if(groupName.isEmpty()) {
        return;
    }

    NewView(this, libName, groupName).exec();
}

/*!*******************************************************************************************************************
 * \brief Triggers creation of new category.
 **********************************************************************************************************************/
void MainWindow::on_actionCategory_triggered()
{
    addNewCategory();
}

/*!*******************************************************************************************************************
 * \brief Creates new session and clears up all current project settings.
 ********************************************************************************************************************/
void MainWindow::on_actionSession_triggered()
{
   if(isStateChanged()) {
       if(askUserForAction("Would you like to save your current session changes?")) {
           on_actionSave_triggered();
       }
   }

   m_ui->treeLibs->clear();
   m_ui->listViews->clear();
   m_ui->listGroups->clear();
   m_ui->listCategories->clear();
   m_ui->listDocumentation->clear();
   refreshSnapshot();

   m_currentProjFile = "";

   if(m_properties) {
       delete m_properties;
   }

   m_properties = new Properties;

   loadSettings();

   setWindowTitle(getAppTitle());
   setupProjectFileWatcher(QString());
   setStateSaved();
}

/*!*******************************************************************************************************************
 * \brief Removes all the project file items from the menu
 ********************************************************************************************************************/
void MainWindow::on_actionClear_Recent_File_Stack_triggered()
{
    if (!libmanAutomatedTestRun()) {
        const QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Clear Recent File Stack"),
            tr("Clear the list of recent project files?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    for (int i = 0; i < PROJ_MAX_COUNT; ++i) {
        m_recentProjects[i]->setText("");
        m_recentProjects[i]->setData("");
        m_recentProjects[i]->setVisible(false);
    }

    QSettings settings(getSettingsHeaderName());
    settings.beginGroup("RecentProjects");
    settings.setValue("RecentProjList", QStringList());
    settings.endGroup();
}

/*!*******************************************************************************************************************
 * \brief Sets up watcher for the current project file to detect external modifications.
 * \param projFile     Absolute path to the current project file.
 **********************************************************************************************************************/
void MainWindow::setupProjectFileWatcher(const QString &projFile)
{
    if (libmanAutomatedTestRun()) {
        Q_UNUSED(projFile);
        return;
    }

    if(!m_projFileWatcher) {
        return;
    }

    QStringList files = m_projFileWatcher->files();
    if(!files.isEmpty()) {
        m_projFileWatcher->removePaths(files);
    }

    if(projFile.isEmpty()) {
        return;
    }

    QFileInfo fi(projFile);
    if(fi.exists() && fi.isFile()) {
        m_projFileWatcher->addPath(fi.absoluteFilePath());
    }
}

/*!*******************************************************************************************************************
 * \brief Clears currently loaded project-related UI data and properties.
 **********************************************************************************************************************/
void MainWindow::clearCurrentProjectData()
{
    m_ui->treeLibs->clear();
    m_ui->listViews->clear();
    m_ui->listGroups->clear();
    m_ui->listCategories->clear();
    m_ui->listDocumentation->clear();
    refreshSnapshot();

    m_itemText.clear();
    m_currentProjFile.clear();
    clearLoadedPdks();
    m_wildcardDefines.clear();

    if(m_properties) {
        delete m_properties;
    }

    m_properties = new Properties;

    loadSettings();

    setWindowTitle(getAppTitle());
    setStateSaved();
}

/*!*******************************************************************************************************************
 * \brief Reloads currently opened project file from disk.
 **********************************************************************************************************************/
void MainWindow::reloadProjectFileFromDisk()
{
    const QString projFile = m_currentProjFile;
    if(projFile.isEmpty()) {
        return;
    }

    if(!QFileInfo(projFile).exists()) {
        error(QString("Project file '%1' does not exist anymore.").arg(projFile), false);
        return;
    }

    clearCurrentProjectData();
    loadProjectFile(projFile);
    setupProjectFileWatcher(projFile);

    info(QString("Project file '%1' was reloaded.").arg(projFile), false);
}

/*!*******************************************************************************************************************
 * \brief Handles external project file modification detected by QFileSystemWatcher.
 * \param path        Changed project file path.
 **********************************************************************************************************************/
void MainWindow::onProjectFileChanged(const QString &path)
{
    if(m_ignoreProjectFileChange) {
        setupProjectFileWatcher(path);
        return;
    }

    if(m_pythonConsole && m_pythonConsole->isBusy()) {
        setupProjectFileWatcher(path);
        return;
    }

    QFileInfo fi(path);

    if(fi.exists() && fi.isFile()) {
        setupProjectFileWatcher(path);
    }

    if (libmanAutomatedTestRun()) {
        return;
    }

    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setWindowTitle("Project File Changed");
    msgBox.setText(QString("The project file\n\n%1\n\nhas been modified outside DOOR.").arg(path));
    msgBox.setInformativeText("Would you like to reload it?");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);

    const int ret = msgBox.exec();
    if(ret == QMessageBox::Yes) {
        reloadProjectFileFromDisk();
    }
    else {
        info(QString("Project file '%1' was modified externally. Reload skipped.").arg(path), false);
    }
}

void MainWindow::applyCoreViewLockPresentation(QTreeWidgetItem *viewItem,
                                              const QString &viewName,
                                              const QString &viewPath)
{
    Q_UNUSED(viewName)

    if(!viewItem || viewPath.isEmpty()) {
        return;
    }

    const CoreFileLockInfo lockInfo = readActiveCoreLockFile(viewPath);
    if(!lockInfo.present) {
        viewItem->setIcon(0, QIcon());
        viewItem->setToolTip(0, viewPath);
        return;
    }

    viewItem->setIcon(0, QIcon(QStringLiteral(":/icons/locked.svg")));

    QString tooltip = viewPath;
    tooltip += QStringLiteral("\n\nLocked");
    if(lockInfo.parseOk) {
        if(!lockInfo.user.isEmpty() || !lockInfo.host.isEmpty()) {
            tooltip += QStringLiteral("\nBy: ") + lockInfo.user;
            if(!lockInfo.host.isEmpty()) {
                tooltip += QStringLiteral(" @ ") + lockInfo.host;
            }
        }
        if(!lockInfo.tool.isEmpty()) {
            tooltip += QStringLiteral("\nTool: ") + lockInfo.tool;
        }
        if(!lockInfo.createdAt.isEmpty()) {
            tooltip += QStringLiteral("\nSince: ") + lockInfo.createdAt;
        }
    }
    else if(!lockInfo.parseError.isEmpty()) {
        tooltip += QStringLiteral("\n") + lockInfo.parseError;
    }

    viewItem->setToolTip(0, tooltip);
}

void MainWindow::syncCoreLockWatches()
{
    if(!m_coreLockWatcher) {
        return;
    }

    QSet<QString> wantedDirs;
    QSet<QString> wantedFiles;

    // Always watch the selected cell directory so externally created views
    // (e.g. EMStudio Output → Create → *.emmodel.room) appear without Reload.
    const QString curLib = getCurrentLibraryName();
    const QString curCell = getCurrentGroupName();
    if(!curLib.isEmpty() && !curCell.isEmpty()) {
        const QString libRoot = getLibraryPath(curLib);
        if(!libRoot.isEmpty()) {
            const QString cellDir = QDir(libRoot).filePath(curCell);
            if(QDir(cellDir).exists()) {
                wantedDirs.insert(normalizedWatchPath(cellDir));
            }
        }
    }

    for(int i = 0; i < m_ui->listViews->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_ui->listViews->topLevelItem(i);
        if(item->data(0, RoleType).toInt() != ItemViewCore) {
            continue;
        }

        const QString corePath = item->data(0, RoleCorePath).toString();
        if(corePath.isEmpty()) {
            continue;
        }

        const QString lockPath = normalizedWatchPath(lockFilePathForCore(corePath));
        wantedDirs.insert(normalizedWatchPath(QFileInfo(lockPath).absolutePath()));
        if(QFileInfo::exists(lockPath)) {
            wantedFiles.insert(lockPath);
        }
    }

    for(const QString &dir : m_coreLockWatchedDirs) {
        if(!wantedDirs.contains(dir)) {
            m_coreLockWatcher->removePath(dir);
        }
    }
    for(const QString &dir : wantedDirs) {
        if(!m_coreLockWatchedDirs.contains(dir)) {
            if(m_coreLockWatcher->addPath(dir)) {
                m_coreLockWatchedDirs.insert(dir);
            }
        }
    }
    m_coreLockWatchedDirs.intersect(wantedDirs);

    for(const QString &file : m_coreLockWatchedFiles) {
        if(!wantedFiles.contains(file)) {
            m_coreLockWatcher->removePath(file);
        }
    }
    for(const QString &file : wantedFiles) {
        if(!m_coreLockWatchedFiles.contains(file)) {
            if(m_coreLockWatcher->addPath(file)) {
                m_coreLockWatchedFiles.insert(file);
            }
        }
    }
    m_coreLockWatchedFiles.intersect(wantedFiles);

    if(m_coreLockPollTimer != nullptr) {
        if(!m_coreLockWatchedFiles.isEmpty()) {
            if(!m_coreLockPollTimer->isActive()) {
                m_coreLockPollTimer->start();
            }
        } else if(m_coreLockPollTimer->isActive()) {
            m_coreLockPollTimer->stop();
        }
    }
}

void MainWindow::refreshCoreViewLockItems(const QString &changedPath)
{
    const QString normalizedChanged = normalizedWatchPath(changedPath);

    for(int i = 0; i < m_ui->listViews->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_ui->listViews->topLevelItem(i);
        if(item->data(0, RoleType).toInt() != ItemViewCore) {
            continue;
        }

        const QString corePath = item->data(0, RoleCorePath).toString();
        if(corePath.isEmpty()) {
            continue;
        }

        if(!normalizedChanged.isEmpty()) {
            const QString lockPath = normalizedWatchPath(lockFilePathForCore(corePath));
            const QString coreDir = QFileInfo(corePath).absolutePath();
            if(!watchPathsEqual(normalizedChanged, lockPath)
               && !watchPathsEqual(normalizedChanged, corePath)
               && !watchPathsEqual(normalizedChanged, coreDir)) {
                continue;
            }
        }

        applyCoreViewLockPresentation(item, item->text(0), corePath);
    }
}

void MainWindow::scheduleCoreLockRefresh()
{
    if(m_coreLockRefreshTimer != nullptr) {
        m_coreLockRefreshTimer->start();
    }
}

void MainWindow::maybeReloadCurrentCellViews()
{
    if(m_reloadingCellViews || !m_ui || !m_ui->listViews) {
        return;
    }

    const QString libName = getCurrentLibraryName();
    const QString cellName = getCurrentGroupName();
    if(libName.isEmpty() || cellName.isEmpty()) {
        m_emModelWatchPath.clear();
        m_emModelWatchMtime = QDateTime();
        return;
    }

    QStringList diskViews = getCurrentViews(libName, cellName);
    QStringList treeViews;
    treeViews.reserve(m_ui->listViews->topLevelItemCount());
    for(int i = 0; i < m_ui->listViews->topLevelItemCount(); ++i) {
        if(QTreeWidgetItem *item = m_ui->listViews->topLevelItem(i)) {
            treeViews << item->text(0);
        }
    }
    diskViews.sort(Qt::CaseInsensitive);
    treeViews.sort(Qt::CaseInsensitive);

    const QString emPath = getViewPath(libName, cellName, QStringLiteral("emmodel"));
    QDateTime emMtime;
    if(!emPath.isEmpty() && QFileInfo::exists(emPath)) {
        emMtime = QFileInfo(emPath).lastModified();
    }

    const bool emmodelOnDisk = diskViews.contains(QStringLiteral("emmodel"), Qt::CaseInsensitive);
    const bool emmodelInTree = treeViews.contains(QStringLiteral("emmodel"), Qt::CaseInsensitive);
    const bool emmodelContentChanged =
        emmodelOnDisk
        && !emPath.isEmpty()
        && !m_emModelWatchPath.isEmpty()
        && QFileInfo(m_emModelWatchPath).absoluteFilePath()
               == QFileInfo(emPath).absoluteFilePath()
        && m_emModelWatchMtime.isValid()
        && emMtime.isValid()
        && emMtime != m_emModelWatchMtime;

    if(diskViews == treeViews) {
        if(emmodelContentChanged) {
            info(tr("Updated emmodel for cell '%1' in library '%2'.")
                     .arg(cellName, libName),
                 false);
            refreshSnapshot();
        }
        m_emModelWatchPath = emPath;
        m_emModelWatchMtime = emMtime;
        return;
    }

    QString selectedTop;
    if(QTreeWidgetItem *cur = m_ui->listViews->currentItem()) {
        while(cur->parent()) {
            cur = cur->parent();
        }
        selectedTop = cur->text(0);
    }

    m_reloadingCellViews = true;
    loadViews(libName, cellName);
    m_reloadingCellViews = false;

    if(!emmodelInTree && emmodelOnDisk) {
        info(tr("Updated emmodel for cell '%1' in library '%2'.")
                 .arg(cellName, libName),
             false);
    } else if(emmodelInTree && !emmodelOnDisk) {
        info(tr("Removed emmodel view for cell '%1' in library '%2'.")
                 .arg(cellName, libName),
             false);
    } else if(emmodelContentChanged) {
        info(tr("Updated emmodel for cell '%1' in library '%2'.")
                 .arg(cellName, libName),
             false);
    }

    m_emModelWatchPath = emPath;
    m_emModelWatchMtime = emMtime;

    if(!selectedTop.isEmpty()) {
        const QList<QTreeWidgetItem *> matches =
            m_ui->listViews->findItems(selectedTop, Qt::MatchExactly);
        if(!matches.isEmpty()) {
            m_ui->listViews->setCurrentItem(matches.first());
        }
    }
}

void MainWindow::onCoreLockWatcherChanged(const QString &path)
{
    refreshCoreViewLockItems(path);
    syncCoreLockWatches();
    scheduleCoreLockRefresh();
}

QString MainWindow::coreLockInfoExtraLines(const QString &corePath) const
{
    return formatCoreLockInfoBlock(readActiveCoreLockFile(corePath));
}

/*!*******************************************************************************************************************
 * \brief Reloads currently opened project file from disk.
 *
 * This slot is triggered by the "Reload" action. It discards all current
 * in-memory project data (libraries, groups, views, categories, caches)
 * and loads the project file again from disk.
 *
 * If no project file is currently loaded or the file does not exist,
 * the operation is aborted and an error message is displayed.
 *
 * \note This function does not prompt for saving changes. It is assumed that
 *       the user intentionally wants to reload the project state from disk.
 **********************************************************************************************************************/
void MainWindow::on_actionReload_triggered()
{
    const QString projFile = getCurrentProjectFile();

    if(projFile.isEmpty()) {
        error("No project file loaded.", false);
        return;
    }

    if(!QFileInfo(projFile).exists()) {
        error(QString("Project file '%1' does not exist.").arg(projFile), false);
        return;
    }

    m_ignoreProjectFileChange = true;

    clearCurrentProjectData();
    loadProjectFile(projFile);
    setupProjectFileWatcher(projFile);

    m_ignoreProjectFileChange = false;

    info(QString("Project '%1' reloaded.").arg(projFile), false);
}

/*!*********************************************************************************************************************
 * \brief Renames the currently selected library.
 *
 * Prompts the user for a new library name using an input dialog. If confirmed,
 * all property keys associated with the selected library are updated to use
 * the new library name prefix.
 *
 * Only internal LibMan property mappings are updated. File system contents
 * (directories and view files) remain unchanged.
 *
 * After successful renaming, the library list is reloaded and the UI is updated.
 **********************************************************************************************************************/
void MainWindow::renameSelectedLibrary()
{
    QTreeWidgetItem *item = m_ui->treeLibs->currentItem();
    if(!item) {
        return;
    }

    const QString oldLibName = item->text(0).trimmed();
    if(oldLibName.isEmpty()) {
        return;
    }

    bool ok = false;
    const QString newLibName = QInputDialog::getText(this,
                                                     tr("Rename Library"),
                                                     tr("Enter new library name:"),
                                                     QLineEdit::Normal,
                                                     oldLibName,
                                                     &ok).trimmed();

    if(!ok || newLibName.isEmpty() || newLibName == oldLibName) {
        return;
    }

    if(!updateProjectFileLibraryName(oldLibName, newLibName)) {
        error("Failed to update project file.", false);
        return;
    }

    const QString oldPrefix = getLibraryKeyPrefix() + oldLibName + "/";
    const QString newPrefix = getLibraryKeyPrefix() + newLibName + "/";

    QMap<QString, PropertyItem*> propItems = m_properties->getMap();
    QList<QString> keysToUpdate;

    for(auto it = propItems.begin(); it != propItems.end(); ++it) {
        const QString key = it.key();
        if(key.startsWith(oldPrefix)) {
            keysToUpdate << key;
        }
    }

    for(const QString &oldKey : keysToUpdate) {
        const QString value = m_properties->get<QString>(oldKey);

        QString newKey = oldKey;
        newKey.replace(oldPrefix, newPrefix);

        m_properties->set(newKey, value);
        m_properties->remove(oldKey);
    }

    loadLibraries();

    info(QString("Library '%1' renamed to '%2'.")
             .arg(oldLibName, newLibName), false);
}

/*!*********************************************************************************************************************
 * \brief Updates library name in the current project file.
 *
 * This function scans the loaded project file and replaces all occurrences
 * of the given old library name with the new one inside library definitions
 * (e.g. define("libName", "...")).
 *
 * The file is read entirely, modified in memory, and then rewritten.
 *
 * \param oldName    Original library name to be replaced.
 * \param newName    New library name to assign.
 *
 * \return True if the project file was successfully updated, false otherwise.
 **********************************************************************************************************************/
bool MainWindow::updateProjectFileLibraryName(const QString &oldName,
                                              const QString &newName)
{
    if(m_currentProjFile.isEmpty()) {
        return false;
    }

    QFile file(m_currentProjFile);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error("Failed to open project file for reading.", false);
        return false;
    }

    QStringList lines;
    QTextStream in(&file);
    while(!in.atEnd()) {
        lines << in.readLine();
    }
    file.close();

    bool changed = false;
    for(QString &line : lines) {
        if(line.contains(QString("\"%1\"").arg(oldName))) {
            line.replace(QString("\"%1\"").arg(oldName),
                         QString("\"%1\"").arg(newName));
            changed = true;
        }
    }

    if(!changed) {
        return false;
    }

    m_ignoreProjectFileChange = true;

    if(m_projFileWatcher) {
        m_projFileWatcher->removePath(m_currentProjFile);
    }

    if(!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if(m_projFileWatcher) {
            m_projFileWatcher->addPath(m_currentProjFile);
        }
        m_ignoreProjectFileChange = false;
        error("Failed to open project file for writing.", false);
        return false;
    }

    QTextStream out(&file);
    for(const QString &line : lines) {
        out << line << "\n";
    }
    file.close();

    if(m_projFileWatcher) {
        m_projFileWatcher->addPath(m_currentProjFile);
    }

    m_ignoreProjectFileChange = false;
    return true;
}

/*!******************************************************************************************************************
 * \brief Retrieves supported view types grouped by tool.
 *
 * This function collects all tools defined in the "ToolList" property and extracts
 * their associated view types from corresponding "<ToolName>Views" properties.
 *
 * Each tool may define a comma-separated list of view suffixes (e.g. "gds,oas,lstr"),
 * which are parsed, normalized (trimmed and converted to lower case), and stored
 * as a list of supported views for that tool.
 *
 * The result is a map where:
 *   - key   = tool name
 *   - value = list of supported view suffixes for that tool
 *
 * Only tools with valid and non-empty view definitions are included.
 *
 * \return QMap mapping tool names to their supported view suffix lists.
 *******************************************************************************************************************/
QMap<QString, QStringList> MainWindow::getSupportedViewsByTool() const
{
    QMap<QString, QStringList> toolViewsMap;
    QStringList tools;

    if(m_properties->exists("ToolList")) {
        tools = m_properties->get<QString>("ToolList").split(",", Qt::SkipEmptyParts);
    }

    for(QString toolName : tools) {
        toolName = toolName.trimmed();
        if(toolName.isEmpty()) {
            continue;
        }

        const QString key = toolName + QStringLiteral("Views");
        if(!m_properties->exists(key)) {
            continue;
        }

        QStringList views =
            m_properties->get<QString>(key).remove(QLatin1Char(' ')).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for(QString &view : views) {
            view = view.trimmed().toLower();
        }
        views.removeAll(QString());

        if(toolName == QStringLiteral("Layout") && !views.contains(QStringLiteral("layout"))) {
            views << QStringLiteral("layout");
        }

        if(views.isEmpty()) {
            continue;
        }

        toolViewsMap.insert(toolName, views);
    }

    return toolViewsMap;
}

/*!*********************************************************************************************************************
 * \brief Adds an existing cell view file to the currently selected library.
 *
 * Opens a file dialog filtered by supported view suffixes, copies the selected file
 * into a newly created cell directory inside the current library, and creates the
 * corresponding LibMan property entry so that the new cell/view becomes visible
 * in the UI.
 *
 * The cell name is derived from the selected file base name.
 **********************************************************************************************************************/
QStringList MainWindow::collectSupportedViewSuffixes() const
{
    QStringList allViews;
    const QMap<QString, QStringList> supportedViewsByTool = getSupportedViewsByTool();

    for(auto it = supportedViewsByTool.constBegin(); it != supportedViewsByTool.constEnd(); ++it) {
        for(const QString &view : it.value()) {
            if(!allViews.contains(view)) {
                allViews << view;
            }
        }
    }

    if(allViews.isEmpty()) {
        allViews = getValidViewList();
        if(!allViews.contains(QStringLiteral("lstr"))) {
            allViews << QStringLiteral("lstr");
        }
        if(!allViews.contains(QStringLiteral("layout"))) {
            allViews << QStringLiteral("layout");
        }
        if(!allViews.contains(QStringLiteral("schematic"))) {
            allViews << QStringLiteral("schematic");
        }
        if(!allViews.contains(QStringLiteral("symbol"))) {
            allViews << QStringLiteral("symbol");
        }
    }

    allViews.sort();
    return allViews;
}

/*!*********************************************************************************************************************
 * \brief Returns true if the drag-and-drop payload contains supported layout view files.
 **********************************************************************************************************************/
bool MainWindow::isSupportedViewDrop(const QMimeData *mimeData) const
{
    if(!mimeData || !mimeData->hasUrls()) {
        return false;
    }

    const QStringList supportedViews = collectSupportedViewSuffixes();
    if(supportedViews.isEmpty()) {
        return false;
    }

    for(const QUrl &url : mimeData->urls()) {
        const QString localPath = url.toLocalFile();
        if(localPath.isEmpty()) {
            continue;
        }

        const RoomViewIdentity coreIdentity = parseRoomViewIdentity(localPath);
        if(coreIdentity.valid && supportedViews.contains(coreIdentity.viewName)) {
            return true;
        }

        const QString viewName = QFileInfo(localPath).suffix().trimmed().toLower();
        if(supportedViews.contains(viewName)) {
            return true;
        }
    }

    return false;
}

bool MainWindow::registerCellViewAtPath(const QString &libName,
                                        const QString &cellName,
                                        const QString &viewName,
                                        const QString &filePath)
{
    if (libName.isEmpty() || cellName.isEmpty() || viewName.isEmpty() || filePath.isEmpty()) {
        return false;
    }

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        error(QString("Cell view file does not exist: %1").arg(filePath), false);
        return false;
    }

    const QStringList allViews = collectSupportedViewSuffixes();
    if (!allViews.contains(viewName)) {
        error(QString("View '%1' is not supported.").arg(viewName), false);
        return false;
    }

    const QString key = getLibraryKeyPrefix() + libName + "/" + cellName + "/" + viewName;
    m_properties->set(key, QDir::toNativeSeparators(fileInfo.absoluteFilePath()));

    for (int i = 0; i < m_ui->treeLibs->topLevelItemCount(); ++i) {
        QTreeWidgetItem *libItem = m_ui->treeLibs->topLevelItem(i);
        if (libItem && libItem->text(0) == libName) {
            m_ui->treeLibs->setCurrentItem(libItem);
            break;
        }
    }

    loadGroups(libName);

    QList<QListWidgetItem*> items = m_ui->listGroups->findItems(cellName, Qt::MatchExactly);
    if (items.count()) {
        m_ui->listGroups->setCurrentItem(items.first());
        loadViews(libName, cellName);
    }

    updateLibraryActionStates();

    if (!m_currentProjFile.isEmpty()) {
        saveProjectFile(m_currentProjFile);
    } else {
        setStateChanged();
    }

    info(QString("Registered cell '%1' in library '%2'.")
             .arg(cellName)
             .arg(libName), false);

    return true;
}

/*!*********************************************************************************************************************
 * \brief Imports an existing layout file into the given library as a new cell/view pair.
 **********************************************************************************************************************/
bool MainWindow::importCellViewFile(const QString &libName, const QString &srcFilePath)
{
    if(libName.isEmpty() || srcFilePath.isEmpty()) {
        return false;
    }

    QFileInfo srcFi(srcFilePath);
    if(!srcFi.exists() || !srcFi.isFile()) {
        error(QString("Selected file does not exist: %1").arg(srcFilePath), false);
        return false;
    }

    QString libRoot = getLibraryPath(libName);
    if(libRoot.isEmpty()) {
        if(!m_currentProjFile.isEmpty()) {
            libRoot = QFileInfo(m_currentProjFile).absoluteDir().absolutePath();
            setLibraryRootDirectory(libName, libRoot);
        }
        else {
            error(QString("Failed to determine root path for library '%1'.").arg(libName), false);
            return false;
        }
    }

    const QStringList allViews = collectSupportedViewSuffixes();
    if(allViews.isEmpty()) {
        error("No supported view suffixes found.", false);
        return false;
    }

    QString groupName;
    QString viewName;
    if(!resolveCellViewFromPath(srcFilePath, &groupName, &viewName)) {
        error(QString("Failed to determine cell/view from '%1'.").arg(srcFilePath), false);
        return false;
    }

    if(groupName.isEmpty()) {
        error(QString("Failed to determine cell name from '%1'.").arg(srcFilePath), false);
        return false;
    }

    if(viewName.isEmpty()) {
        error(QString("Failed to determine view type from '%1'.").arg(srcFilePath), false);
        return false;
    }

    if(!allViews.contains(viewName)) {
        error(QString("View '%1' is not supported.").arg(viewName), false);
        return false;
    }

    const QString cellDirPath = QDir::toNativeSeparators(libRoot + "/" + groupName);
    QDir dir;
    if(!dir.mkpath(cellDirPath)) {
        error(QString("Failed to create cell directory '%1'.").arg(cellDirPath), false);
        return false;
    }

    const QString dstFilePath = QDir::toNativeSeparators(
        isRoomViewName(viewName)
            ? roomViewFilePath(cellDirPath, groupName, viewName)
            : cellDirPath + "/" + groupName + "." + viewName);
    if(QFileInfo(dstFilePath).exists()) {
        error(QString("Cell view already exists: %1").arg(dstFilePath), false);
        return false;
    }

    if(!QFile::copy(srcFilePath, dstFilePath)) {
        error(QString("Failed to copy '%1' to '%2'.").arg(srcFilePath, dstFilePath), false);
        return false;
    }

    return registerCellViewAtPath(libName, groupName, viewName, dstFilePath);
}

/*!*********************************************************************************************************************
 * \brief Handles drag-and-drop of supported layout files onto library or cell panes.
 **********************************************************************************************************************/
void MainWindow::handleViewFileDrop(QDropEvent *event)
{
    if(!event || !event->mimeData()) {
        return;
    }

    const QString libName = getCurrentLibraryName();
    if(libName.isEmpty()) {
        error(tr("Select a library before dropping layout files."), false);
        event->ignore();
        return;
    }

    bool imported = false;
    for(const QUrl &url : event->mimeData()->urls()) {
        const QString localPath = url.toLocalFile();
        if(localPath.isEmpty()) {
            continue;
        }

        if(importCellViewFile(libName, localPath)) {
            imported = true;
        }
    }

    if(imported) {
        event->acceptProposedAction();
    }
    else {
        event->ignore();
    }
}

/*!*********************************************************************************************************************
 * \brief Opens a file dialog and imports an existing layout file into the selected library.
 **********************************************************************************************************************/
void MainWindow::addExistingCell()
{
    const QString libName = getCurrentLibraryName();
    if(libName.isEmpty()) {
        return;
    }

    const QStringList allViews = collectSupportedViewSuffixes();
    if(allViews.isEmpty()) {
        error("No supported views configured.", false);
        return;
    }

    QStringList filters;
    QStringList allPatterns;
    for(const QString &view : allViews) {
        allPatterns << QString("*.%1").arg(view);
    }
    filters << tr("Supported Views (%1)").arg(allPatterns.join(" "));

    for(const QString &view : allViews) {
        filters << QString("%1 (*.%2)").arg(view.toUpper(), view);
    }

    filters << tr("All Files (*.*)");

    const QString srcFilePath = QFileDialog::getOpenFileName(this,
                                                             tr("Add Existing Cell"),
                                                             QString(),
                                                             filters.join(";;"));
    if(srcFilePath.isEmpty()) {
        return;
    }

    importCellViewFile(libName, srcFilePath);
}
