#include "src/klayout_tools.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

#ifdef Q_OS_UNIX
#include <signal.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

bool isKLayoutXorStartupNoise(const QString &line)
{
    static const QStringList patterns = {
        QStringLiteral("RubyGems"),
        QStringLiteral("error_highlight"),
        QStringLiteral("did_you_mean"),
        QStringLiteral("syntax_suggest"),
        QStringLiteral("built-in-macros"),
        QStringLiteral("cannot load such file -- pathname"),
    };

    for(const QString &pattern : patterns) {
        if(line.contains(pattern, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

struct ToolCommand {
    QString program;
    QStringList prefixArgs;
};

ToolCommand splitToolCommand(const QString &tool)
{
    ToolCommand out;
    const QString trimmed = tool.trimmed();
    if(trimmed.isEmpty()) {
        return out;
    }

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    QStringList parts = QProcess::splitCommand(trimmed);
#else
    QStringList parts = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
#endif
    if(parts.isEmpty()) {
        return out;
    }

    out.program = parts.takeFirst();
    out.prefixArgs = parts;
    return out;
}

QString resolveToolProgram(const QString &tool)
{
    const QString program = tool.trimmed();
    if(program.isEmpty()) {
        return QString();
    }

    const QFileInfo fi(program);
    if(fi.isAbsolute() && fi.exists()) {
        return fi.absoluteFilePath();
    }

    const QString found = QStandardPaths::findExecutable(program);
    if(!found.isEmpty()) {
        return found;
    }

    return program;
}

/*!*******************************************************************************************************************
 * \brief Resolve wrappers (…/bin/klayout → klayout-run.sh → dist-bundle/) to the real binary directory.
 **********************************************************************************************************************/
QString resolveKLayoutBundleDir(const QString &toolProgram)
{
    QString path = QFileInfo(toolProgram).absoluteFilePath();
    if(path.isEmpty()) {
        return {};
    }

    // Follow symlinks and tiny launcher scripts that `exec` the real binary.
    for(int hop = 0; hop < 8; ++hop) {
        const QFileInfo fi(path);
        if(fi.isSymLink()) {
            const QString target = fi.symLinkTarget();
            if(target.isEmpty()) {
                break;
            }
            path = QFileInfo(fi.dir(), target).absoluteFilePath();
            continue;
        }

        if(!fi.isFile() || !fi.isReadable() || fi.size() > 65536) {
            break;
        }

        QFile f(path);
        if(!f.open(QIODevice::ReadOnly)) {
            break;
        }
        const QByteArray head = f.read(512);
        f.close();
        if(!head.startsWith("#!")) {
            break;
        }

        // Match: exec "/abs/path/klayout-run.sh" "$@"  or  exec "$DIR/klayout"
        const QString text = QString::fromUtf8(head);
        QRegularExpression re(QStringLiteral(R"_(exec\s+(?:\"([^\"]+)\"|'([^']+)'|(\S+)))_"));
        const QRegularExpressionMatch m = re.match(text);
        if(!m.hasMatch()) {
            break;
        }
        QString next = m.captured(1);
        if(next.isEmpty()) {
            next = m.captured(2);
        }
        if(next.isEmpty()) {
            next = m.captured(3);
        }
        next.replace(QStringLiteral("\"$@\""), QString());
        next = next.trimmed();
        if(next.isEmpty() || next.startsWith(QLatin1Char('$'))) {
            // Relative/$DIR launchers: prefer sibling real klayout next to the script.
            const QString sibling = fi.absolutePath() + QStringLiteral("/klayout");
            if(QFileInfo::exists(sibling) && QFileInfo(sibling).isExecutable()) {
                path = sibling;
                break;
            }
            break;
        }
        path = QFileInfo(next).absoluteFilePath();
    }

    const QFileInfo resolved(path);
    QString dir = resolved.absolutePath();
    // If we landed on klayout-run.sh, the binary sits beside it.
    if(resolved.fileName().contains(QStringLiteral("klayout-run"), Qt::CaseInsensitive)) {
        const QString sibling = dir + QStringLiteral("/klayout");
        if(QFileInfo::exists(sibling)) {
            return dir;
        }
    }
    return dir;
}

bool isProcessAlive(qint64 pid)
{
    if(pid <= 0) {
        return false;
    }

#ifdef Q_OS_UNIX
    return kill(static_cast<pid_t>(pid), 0) == 0;
#elif defined(Q_OS_WIN)
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if(!handle) {
        return false;
    }

    DWORD exitCode = 0;
    const bool alive =
        GetExitCodeProcess(handle, &exitCode) != 0 &&
        exitCode == STILL_ACTIVE;
    CloseHandle(handle);
    return alive;
#else
    Q_UNUSED(pid);
    return false;
#endif
}

QStringList discoverKLayoutLibraryDirs(const QString &toolDir)
{
    QStringList dirs;
    const QStringList candidates = {
        toolDir + QStringLiteral("/lib"),
        toolDir + QStringLiteral("/lib64"),
        toolDir + QStringLiteral("/db_plugins"),
        toolDir + QStringLiteral("/lay_plugins"),
        toolDir,
    };

    for(const QString &candidate : candidates) {
        if(QDir(candidate).exists()) {
            const QString normalized = QFileInfo(candidate).absoluteFilePath();
            if(!dirs.contains(normalized)) {
                dirs << normalized;
            }
        }
    }

    return dirs;
}

QStringList discoverQtPluginDirs(const QString &toolDir)
{
    QStringList dirs;
    const QStringList candidates = {
        toolDir + QStringLiteral("/lib/qt/plugins"),
        toolDir + QStringLiteral("/lib64/qt/plugins"),
        toolDir + QStringLiteral("/plugins"),
        toolDir + QStringLiteral("/qt/plugins"),
    };

    for(const QString &candidate : candidates) {
        if(QDir(candidate).exists()) {
            const QString normalized = QFileInfo(candidate).absoluteFilePath();
            if(!dirs.contains(normalized)) {
                dirs << normalized;
            }
        }
    }

    return dirs;
}

QString klayoutAliveFilePath(const QString &cmdFile)
{
    return cmdFile + QStringLiteral(".alive");
}

QProcessEnvironment buildKLayoutLaunchEnvironment(const QProcessEnvironment &parent,
                                                    const QString &toolProgram,
                                                    const QString &projectFile)
{
    QProcessEnvironment env;

    static const char *kPassthrough[] = {
        "DISPLAY",
        "XAUTHORITY",
        "XDG_RUNTIME_DIR",
        "WAYLAND_DISPLAY",
        "DBUS_SESSION_BUS_ADDRESS",
        "HOME",
        "USER",
        "LOGNAME",
        "LANG",
        "LC_ALL",
        "LC_CTYPE",
        "SHELL",
        "PATH",
        "TMPDIR",
        "TMP",
        "TEMP",
        "SSH_AUTH_SOCK",
        "SESSION_MANAGER",
        "DESKTOP_SESSION",
        "XDG_SESSION_TYPE",
        "XDG_CURRENT_DESKTOP",
#ifdef Q_OS_WIN
        "SystemRoot",
        "WINDIR",
        "USERPROFILE",
        "HOMEDRIVE",
        "HOMEPATH",
        "APPDATA",
        "LOCALAPPDATA",
        "ProgramFiles",
        "ProgramFiles(x86)",
        "CommonProgramFiles",
#endif
        nullptr
    };

    for(int i = 0; kPassthrough[i] != nullptr; ++i) {
        const QString key = QString::fromLatin1(kPassthrough[i]);
        if(parent.contains(key)) {
            env.insert(key, parent.value(key));
        }
    }

#ifdef Q_OS_UNIX
    const QFileInfo toolFi(toolProgram);
    if(toolFi.exists()) {
        // Follow …/bin/klayout wrappers into the real dist-bundle/ for libs + plugins.
        const QString toolDir = resolveKLayoutBundleDir(toolProgram);
        const QStringList libDirs = discoverKLayoutLibraryDirs(toolDir);
        if(!libDirs.isEmpty()) {
            env.insert(QStringLiteral("LD_LIBRARY_PATH"), libDirs.join(QLatin1Char(':')));
        }

        const QStringList pluginDirs = discoverQtPluginDirs(toolDir);
        if(!pluginDirs.isEmpty()) {
            env.insert(QStringLiteral("QT_PLUGIN_PATH"), pluginDirs.join(QLatin1Char(':')));
        }
    }
#elif defined(Q_OS_WIN)
    const QFileInfo toolFi(toolProgram);
    if(toolFi.exists()) {
        const QString toolDir = QDir::toNativeSeparators(toolFi.absolutePath());
        QString path = env.value(QStringLiteral("PATH"));
        if(path.isEmpty()) {
            path = parent.value(QStringLiteral("PATH"));
        }
        if(!path.isEmpty()) {
            env.insert(QStringLiteral("PATH"), toolDir + QLatin1Char(';') + path);
        }
        else {
            env.insert(QStringLiteral("PATH"), toolDir);
        }

        const QStringList pluginDirs = discoverQtPluginDirs(toolDir);
        if(!pluginDirs.isEmpty()) {
            env.insert(QStringLiteral("QT_PLUGIN_PATH"), pluginDirs.join(QLatin1Char(';')));
        }
    }
#endif

    if(!projectFile.isEmpty()) {
        const QFileInfo projFi(projectFile);
        if(projFi.exists() && projFi.isFile()) {
            env.insert(QStringLiteral("KLAYOUT_LIB"),
                       QDir::toNativeSeparators(projFi.absoluteFilePath()));
        }
    }

    return env;
}

bool launchKLayoutDetached(const QString &program,
                           const QStringList &args,
                           const QProcessEnvironment &env,
                           qint64 *pid)
{
    if(pid) {
        *pid = 0;
    }

    const QFileInfo programFi(program);
    QProcess launcher;
    launcher.setProgram(program);
    launcher.setArguments(args);
    launcher.setProcessEnvironment(env);
#ifdef Q_OS_UNIX
    const QString bundleDir = resolveKLayoutBundleDir(program);
    launcher.setWorkingDirectory(bundleDir.isEmpty() ? programFi.absolutePath() : bundleDir);
#else
    launcher.setWorkingDirectory(programFi.absolutePath());
#endif
    return launcher.startDetached(pid);
}

bool isKLayoutAliveFileFresh(const QString &aliveFile, int maxAgeMs = 2000)
{
    const QFileInfo fi(aliveFile);
    if(!fi.exists()) {
        return false;
    }

    const qint64 ageMs = fi.lastModified().msecsTo(QDateTime::currentDateTime());
    return ageMs >= 0 && ageMs <= maxAgeMs;
}

qint64 readKLayoutAlivePid(const QString &aliveFile)
{
    QFile file(aliveFile);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }

    return QString::fromUtf8(file.readAll()).trimmed().toLongLong();
}

} // namespace

KLayoutTools::KLayoutTools(QObject *parent)
    : QObject(parent)
{
}

/*!*******************************************************************************************************************
 * \brief Returns true when the detached KLayout server process is still running.
 **********************************************************************************************************************/
bool KLayoutTools::isServerRunning() const
{
    if(!m_cmdFile.isEmpty()) {
        const QString aliveFile = klayoutAliveFilePath(m_cmdFile);
        const qint64 alivePid = readKLayoutAlivePid(aliveFile);
        if(isProcessAlive(alivePid)) {
            return true;
        }
        if(isKLayoutAliveFileFresh(aliveFile)) {
            return true;
        }
    }

    return isProcessAlive(m_serverPid);
}

/*!*******************************************************************************************************************
 * \brief Sends an "open GDS" request to a running KLayout server instance.
 *
 * The request is written as JSON into a command file polled by KLayout.
 * If \c cellName is empty, the whole layout is opened.
 *
 * \param gdsPath   Absolute or relative path to the GDS file.
 * \param cellName  Optional cell name to activate.
 * \return true on success, false otherwise.
 **********************************************************************************************************************/
bool KLayoutTools::sendOpenRequest(const QString &gdsPath, const QString &cellName)
{
    if(m_cmdFile.isEmpty()) {
        return false;
    }

    const QString fileAbs = QFileInfo(gdsPath).absoluteFilePath();
    const QString tmp = m_cmdFile + ".tmp";

    QJsonObject obj;
    obj["action"] = "open";
    obj["file"]   = fileAbs;
    obj["cell"]   = cellName;

    const QByteArray payload =
        QJsonDocument(obj).toJson(QJsonDocument::Compact);

    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    f.write(payload);
    f.close();

    QFile::remove(m_cmdFile);
    if(!QFile::rename(tmp, m_cmdFile)) {
        QFile f2(m_cmdFile);
        if (!f2.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            return false;
        }
        f2.write(payload);
        f2.close();
    }

    return true;
}

/*!*******************************************************************************************************************
 * \brief Asks the KLayout server to open master + overlay layouts in the same view.
 **********************************************************************************************************************/
bool KLayoutTools::sendOpenOverlayRequest(const QString &masterPath,
                                          const QString &masterCell,
                                          const QString &overlayPath,
                                          const QString &overlayCell)
{
    if(m_cmdFile.isEmpty()) {
        return false;
    }

    const QString tmp = m_cmdFile + ".tmp";

    QJsonObject obj;
    obj["action"] = "open_overlay";
    obj["file"] = QFileInfo(masterPath).absoluteFilePath();
    obj["cell"] = masterCell;
    obj["overlay"] = QFileInfo(overlayPath).absoluteFilePath();
    obj["overlay_cell"] = overlayCell;

    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    QFile f(tmp);
    if(!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    f.write(payload);
    f.close();

    QFile::remove(m_cmdFile);
    if(!QFile::rename(tmp, m_cmdFile)) {
        QFile f2(m_cmdFile);
        if(!f2.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            return false;
        }
        f2.write(payload);
        f2.close();
    }

    return true;
}

/*!*******************************************************************************************************************
 * \brief Ensures that a KLayout instance with a polling server script is running.
 *
 * If a KLayout process is already running, this function does nothing.
 * Otherwise it starts KLayout with a generated server script that polls
 * a command JSON file.
 *
 * \param tool  Executable name or absolute path to KLayout.
 * \return true if KLayout is running or was successfully started.
 **********************************************************************************************************************/
bool KLayoutTools::ensureServerRunning(const QString &tool, const QString &projectFile)
{
    if(tool.isEmpty()) {
        return false;
    }

    if(isServerRunning()) {
        if(!m_cmdFile.isEmpty()) {
            const qint64 alivePid = readKLayoutAlivePid(klayoutAliveFilePath(m_cmdFile));
            if(isProcessAlive(alivePid)) {
                m_serverPid = alivePid;
            }
        }
        return true;
    }

    m_serverPid = 0;

    if(m_cmdFile.isEmpty()) {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        m_cmdFile = QDir::toNativeSeparators(
            base + "/libman_klayout_cmd_" +
            QString::number(QCoreApplication::applicationPid()) + ".json");
    }

    {
        QFile f(m_cmdFile);
        if(f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write("");
            f.close();
        }
    }
    QFile::remove(klayoutAliveFilePath(m_cmdFile));

    if(!m_serverScript.isEmpty()) {
        QFile::remove(m_serverScript);
        m_serverScript.clear();
    }

    m_serverScript = createServerScript(m_cmdFile);
    if(m_serverScript.isEmpty() || !QFileInfo(m_serverScript).exists()) {
        emit error(QString("Failed to create KLayout server script in %1").arg(QDir::tempPath()));
        return false;
    }

    const ToolCommand toolCmd = splitToolCommand(tool);
    if(toolCmd.program.isEmpty()) {
        return false;
    }

    const QString program = resolveToolProgram(toolCmd.program);
    const QFileInfo programFi(program);
    if(!programFi.exists()) {
        emit error(QString("KLayout executable not found: %1").arg(toolCmd.program));
        return false;
    }

    QStringList args = toolCmd.prefixArgs;
    args << QStringLiteral("-rr") << m_serverScript;

    QString cmdLine = program;
    for(const QString &arg : args) {
        if(arg.contains(QLatin1Char(' '))) {
            cmdLine += QLatin1String(" \"") + arg + QLatin1Char('"');
        }
        else {
            cmdLine += QLatin1Char(' ') + arg;
        }
    }

    const QProcessEnvironment env =
        buildKLayoutLaunchEnvironment(QProcessEnvironment::systemEnvironment(),
                                      program,
                                      projectFile);

    qint64 pid = 0;
    const bool started = launchKLayoutDetached(program, args, env, &pid);
    if(!started || pid <= 0) {
        emit error(QString("Failed to start KLayout server: %1").arg(cmdLine));
        return false;
    }

    m_serverPid = pid;

    const qint64 startupDeadlineMs = QDateTime::currentMSecsSinceEpoch() + 8000;
    while(QDateTime::currentMSecsSinceEpoch() < startupDeadlineMs) {
        if(isServerRunning()) {
            break;
        }
        QThread::msleep(100);
    }

    if(!isServerRunning()) {
        m_serverPid = 0;
        QString details = QString("KLayout server exited immediately: %1").arg(cmdLine);
#ifdef Q_OS_UNIX
        const QString ldPath = env.value(QStringLiteral("LD_LIBRARY_PATH"));
        const QString qtPlugins = env.value(QStringLiteral("QT_PLUGIN_PATH"));
        if(!ldPath.isEmpty()) {
            details += QString("\nLD_LIBRARY_PATH=%1").arg(ldPath);
        }
        if(!qtPlugins.isEmpty()) {
            details += QString("\nQT_PLUGIN_PATH=%1").arg(qtPlugins);
        }
        details += QString(
            "\nIf Qt reports xcb plugin errors, ensure system libs such as "
            "libxcb-xinerama0 and libxkbcommon-x11 are installed.");
#endif
        emit error(details);
        return false;
    }

    emit info(QString("Starting KLayout server: %1").arg(cmdLine));

    return true;
}

bool KLayoutTools::sendSelectRequest(const QString &gdsPath, const QString &cellName)
{
    if(m_cmdFile.isEmpty()) {
        return false;
    }

    const QString tmp = m_cmdFile + ".tmp";
    const QString fileAbs = QFileInfo(gdsPath).absoluteFilePath();

    QJsonObject obj;
    obj["action"] = "select";
    obj["file"]   = fileAbs;
    obj["cell"]   = cellName;

    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    f.write(payload);
    f.close();

    QFile::remove(m_cmdFile);
    if(!QFile::rename(tmp, m_cmdFile)) {
        QFile f2(m_cmdFile);
        if (!f2.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            return false;
        }
        f2.write(payload);
        f2.close();
    }

    return true;
}

/*!*******************************************************************************************************************
 * \brief Creates a persistent KLayout server script.
 *
 * The script polls a JSON command file and reacts to commands:
 *  - action="open":   opens/loads a GDS file into an existing view and optionally selects a cell.
 *  - action="select": selects a cell in an already opened view (does not load a file).
 *
 * The script also tries to raise/activate the KLayout main window to make the action visible.
 *
 * \param cmdFile  Absolute path to the command JSON file.
 * \return Absolute path to the generated Python script.
 **********************************************************************************************************************/
QString KLayoutTools::createServerScript(const QString &cmdFile) const
{
    auto pyRaw = [](const QString &s) -> QString {
        QString t = QDir::toNativeSeparators(s);
        t.replace("\\", "\\\\");
        t.replace("'", "\\'");
        return QString("r'%1'").arg(t);
    };

    QTemporaryFile tf(QDir::tempPath() +
                      QDir::separator() +
                      "libman_klayout_server_XXXXXX.py");
    tf.setAutoRemove(false);

    if (!tf.open()) {
        return QString();
    }

    QTextStream out(&tf);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#else
    out.setEncoding(QStringConverter::Utf8);
#endif

    out <<
        R"(# -*- coding: utf-8 -*-
import pya
import os
import json

CMD_FILE = )" << pyRaw(cmdFile) << R"(
ALIVE_FILE = CMD_FILE + ".alive"

def _touch_alive():
    try:
        with open(ALIVE_FILE, "w", encoding="utf-8") as f:
            f.write(str(os.getpid()))
    except Exception:
        pass

_app = pya.Application.instance()
_mw  = _app.main_window() if _app is not None else None

def _norm(p):
    try:
        return os.path.normcase(os.path.normpath(p))
    except Exception:
        return p

def _raise_main_window():
    # Make action visible: bring KLayout to front
    try:
        _mw.raise_()
        _mw.activateWindow()
    except Exception:
        pass

def _find_view_for_file(fn):
    if _mw is None:
        return (None, None, -1, -1)
    for lv_idx in range(_mw.views()):
        lv = _mw.view(lv_idx)
        for i in range(lv.cellviews()):
            cv = lv.cellview(i)
            try:
                if _norm(cv.filename()) == _norm(fn):
                    return (lv, cv, lv_idx, i)
            except Exception:
                pass
    return (None, None, -1, -1)

def _open_or_load(fn):
    # Ensure file is loaded into SOME view (same-view mode)
    (lv, cv, lv_idx, cv_idx) = _find_view_for_file(fn)
    if cv_idx == -1:
        _mw.load_layout(fn, 1)   # 1 = same view
        (lv, cv, lv_idx, cv_idx) = _find_view_for_file(fn)
    return (lv, cv, lv_idx, cv_idx)

def _select_cell(lv, cv, cv_idx, cell):
    if not cell:
        return False
    try:
        c = cv.layout().cell_by_name(cell)
        if c is not None:
            lv.select_cell(c, cv_idx)
            return True
    except Exception:
        pass
    return False

def _zoom_fit_delayed():
    # Do zoom_fit only when view is ready
    try:
        mw2 = pya.Application.instance().main_window()
        lv2 = mw2.current_view() if mw2 is not None else None
        ready = (lv2 is not None) and (lv2.cellviews() > 0) and (lv2.active_cellview() is not None)
        if ready:
            try:
                lv2.zoom_fit()
            except Exception:
                pass
            return True
    except Exception:
        pass
    return False

def _schedule_zoom_fit():
    global _fit_timer
    try:
        _fit_timer
    except NameError:
        _fit_timer = None

    if _fit_timer is None:
        _fit_timer = pya.QTimer(_mw)
        _fit_timer.setSingleShot(True)

        def _try_fit():
            if not _zoom_fit_delayed():
                _fit_timer.start(200)

        _fit_timer.timeout(_try_fit)

    if _fit_timer.isActive():
        _fit_timer.stop()
    _fit_timer.start(200)

def _handle(cmd):
    if _mw is None:
        return

    action = cmd.get("action", "")
    fn     = cmd.get("file", "")
    cell   = cmd.get("cell", "")

    if action not in ("open", "select", "open_overlay"):
        return

    if action == "open_overlay":
        overlay = cmd.get("overlay", "")
        overlay_cell = cmd.get("overlay_cell", "") or "XOR"
        if not fn or not overlay:
            return
        if not os.path.exists(fn) or not os.path.exists(overlay):
            return

        (lv, cv, lv_idx, cv_idx) = _open_or_load(fn)
        if lv is None:
            return
        _mw.select_view(lv_idx)
        _select_cell(lv, cv, cv_idx, cell)

        (lv2, cv2, lv_idx2, cv_idx2) = _open_or_load(overlay)
        if lv2 is not None:
            _mw.select_view(lv_idx2)
            _select_cell(lv2, cv2, cv_idx2, overlay_cell)

        _raise_main_window()
        _schedule_zoom_fit()
        return

    if not fn:
        return

    # For "select" do not load file - only operate if file is already open
    if action == "select":
        (lv, cv, lv_idx, cv_idx) = _find_view_for_file(fn)
        if lv is None:
            return
        _mw.select_view(lv_idx)
        _select_cell(lv, cv, cv_idx, cell)
        _raise_main_window()
        _schedule_zoom_fit()
        return

    # action == "open": load if needed
    if not os.path.exists(fn):
        return

    (lv, cv, lv_idx, cv_idx) = _open_or_load(fn)
    if lv is None:
        return

    _mw.select_view(lv_idx)
    _select_cell(lv, cv, cv_idx, cell)

    _raise_main_window()
    _schedule_zoom_fit()

def _poll():
    _touch_alive()
    if not os.path.exists(CMD_FILE):
        return
    try:
        txt = open(CMD_FILE, "r", encoding="utf-8").read()
        if not txt.strip():
            return
        cmd = json.loads(txt)
        open(CMD_FILE, "w").close()
        _handle(cmd)
    except Exception:
        try:
            open(CMD_FILE, "w").close()
        except Exception:
            pass

if _mw is not None:
    _touch_alive()
    _t = pya.QTimer(_mw)
    _t.timeout(_poll)
    _t.start(250)
)";

    out.flush();
    tf.close();
    return tf.fileName();
}

namespace {

QString pyRawString(const QString &s)
{
    QString t = QDir::toNativeSeparators(s);
    t.replace("\\", "\\\\");
    t.replace("'", "\\'");
    return QString("r'%1'").arg(t);
}

QString resolveToolExecutable(const QString &tool)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    const QStringList parts = QProcess::splitCommand(tool.trimmed());
#else
    const QStringList parts = tool.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
#endif
    if(parts.isEmpty()) {
        return QString();
    }

    QString program = parts.first();
    const QFileInfo fi(program);
    if(!(fi.isAbsolute() && fi.exists())) {
        const QString found = QStandardPaths::findExecutable(program);
        if(!found.isEmpty()) {
            program = found;
        }
    }

    return program;
}

} // namespace

void KLayoutTools::openLayoutFile(const QString &tool,
                                  const QString &layoutPath,
                                  const QString &cellName,
                                  const QString &projectFile)
{
    const QString absPath = QFileInfo(layoutPath).absoluteFilePath();
    if(absPath.isEmpty() || !QFileInfo::exists(absPath)) {
        emit error(QObject::tr("Layout file not found: %1").arg(layoutPath));
        return;
    }

    if(tool.isEmpty()) {
        emit error(QObject::tr("Please configure the Layout tool in Tool Manager first."));
        return;
    }

    if(isServerRunning()) {
        sendOpenRequest(absPath, cellName);
        return;
    }

    if(ensureServerRunning(tool, projectFile)) {
        sendOpenRequest(absPath, cellName);
        return;
    }

    const QString scriptPath = createOpenScript(absPath, cellName);
    if(scriptPath.isEmpty()) {
        emit error(QObject::tr("Failed to create KLayout open script."));
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    const QStringList parts = QProcess::splitCommand(tool.trimmed());
#else
    const QStringList parts = tool.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
#endif
    if(parts.isEmpty()) {
        emit error(QObject::tr("Invalid Layout tool command."));
        QFile::remove(scriptPath);
        return;
    }

    const QString program = resolveToolExecutable(tool);
    QStringList args = parts.mid(1);
    args << QStringLiteral("-r") << scriptPath;
    startToolWithTempScript(program, args, scriptPath);
}

void KLayoutTools::openLayoutWithOverlay(const QString &tool,
                                         const QString &masterPath,
                                         const QString &masterCell,
                                         const QString &overlayPath,
                                         const QString &overlayCell,
                                         const QString &projectFile)
{
    const QString absMaster = QFileInfo(masterPath).absoluteFilePath();
    const QString absOverlay = QFileInfo(overlayPath).absoluteFilePath();
    if(absMaster.isEmpty() || !QFileInfo::exists(absMaster)) {
        emit error(QObject::tr("Master layout file not found: %1").arg(masterPath));
        return;
    }
    if(absOverlay.isEmpty() || !QFileInfo::exists(absOverlay)) {
        emit error(QObject::tr("XOR layout file not found: %1").arg(overlayPath));
        return;
    }

    if(tool.isEmpty()) {
        emit error(QObject::tr("Please configure the Layout tool in Tool Manager first."));
        return;
    }

    const QString overlayCellName =
        overlayCell.isEmpty() ? QStringLiteral("XOR") : overlayCell;

    if(isServerRunning() || ensureServerRunning(tool, projectFile)) {
        // Two "open" commands: existing servers already load into the same view
        // (load_layout mode 1). A short gap lets the first load settle.
        sendOpenRequest(absMaster, masterCell);
        QTimer::singleShot(400, this, [this, absOverlay, overlayCellName]() {
            sendOpenRequest(absOverlay, overlayCellName);
        });
        return;
    }

    const QString scriptPath =
        createOpenWithOverlayScript(absMaster, masterCell, absOverlay, overlayCellName);
    if(scriptPath.isEmpty()) {
        emit error(QObject::tr("Failed to create KLayout open script."));
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    const QStringList parts = QProcess::splitCommand(tool.trimmed());
#else
    const QStringList parts = tool.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
#endif
    if(parts.isEmpty()) {
        emit error(QObject::tr("Invalid Layout tool command."));
        QFile::remove(scriptPath);
        return;
    }

    const QString program = resolveToolExecutable(tool);
    QStringList args = parts.mid(1);
    args << QStringLiteral("-r") << scriptPath;
    startToolWithTempScript(program, args, scriptPath);
}

QString KLayoutTools::createOpenScript(const QString &gdsPath, const QString &cellName) const
{
    QTemporaryFile tf(QDir::tempPath() + QDir::separator() + "libman_klayout_open_cell_XXXXXX.py");
    tf.setAutoRemove(false);

    if(!tf.open()) {
        return QString();
    }

    QTextStream out(&tf);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#else
    out.setEncoding(QStringConverter::Utf8);
#endif

    out <<
        R"(# -*- coding: utf-8 -*-
import pya
import os
import os.path

_app = pya.Application.instance()
_mw  = _app.main_window() if _app is not None else None


#==============================================================================
def libman_cmp_paths(p1, p2):
    p1 = os.path.normcase(os.path.normpath(p1))
    p2 = os.path.normcase(os.path.normpath(p2))
    return p1 == p2


#==============================================================================
# Delayed zoom_fit (wait until the view is fully ready)
def libman_fit_view_to_window():
    global _app, _mw
    if _app is None:
        _app = pya.Application.instance()
    if _mw is None and _app is not None:
        _mw = _app.main_window()
    if _mw is None:
        return

    global _libman_fit_timer
    try:
        _libman_fit_timer
    except NameError:
        _libman_fit_timer = None

    if _libman_fit_timer is None:
        t = pya.QTimer(_mw)
        t.setSingleShot(True)

        def _on_timeout():
            app2 = pya.Application.instance()
            mw2  = app2.main_window() if app2 is not None else None
            lv2  = mw2.current_view() if mw2 is not None else None

            ready = (lv2 is not None) and (lv2.cellviews() > 0) and (lv2.active_cellview() is not None)

            if ready:
                try:
                    lv2.zoom_fit()
                except Exception:
                    pass
            else:
                t.start(200)

        t.timeout(_on_timeout)
        _libman_fit_timer = t

    if _libman_fit_timer.isActive():
        _libman_fit_timer.stop()
    _libman_fit_timer.start(200)


#==============================================================================
class LibManRequest:
    def open_cell(self, file_name, cell_name):
        if _mw is None:
            return

        # Ensure layout is loaded
        if not os.path.exists(file_name):
            # Only meaningful to create a layout when a cell name is given
            if not cell_name:
                return
            (lv, cv, lv_idx, cv_idx, need_save) = self.libman_create_layout(file_name, cell_name)
        else:
            (lv, cv, lv_idx, cv_idx, need_save) = self.libman_open_layout(file_name)

        if lv is None or cv is None or cv_idx < 0:
            return

        _mw.select_view(lv_idx)

        # If no cell requested: just open file (zoom_fit will be applied later)
        if not cell_name:
            return

        # Select requested cell if it exists
        try:
            top_cell = cv.layout().cell_by_name(cell_name)
            if top_cell is not None:
                lv.select_cell(top_cell, cv_idx)
        except Exception:
            pass


    def libman_create_layout(self, file_name, cell_name):
        # Create a new layout in a new view.
        cv = _mw.create_layout(1)

        # Add cell.
        cv.layout().add_cell(cell_name)

        # Save file.
        (lv, cv_idx) = self.libman_get_view_and_index(cv)
        lv.save_as(cv_idx, file_name, False, pya.SaveLayoutOptions())

        (lv, cv, lv_idx, cv_idx) = self.libman_find_view_for_file(file_name)
        return (lv, cv, lv_idx, cv_idx, False)  # do not save


    def libman_open_layout(self, file_name):
        (lv, cv, lv_idx, cv_idx) = self.libman_find_view_for_file(file_name)
        if cv_idx == -1:
            # Load into existing view (same-view mode)
            _mw.load_layout(file_name, 1)
            (lv, cv, lv_idx, cv_idx) = self.libman_find_view_for_file(file_name)
        return (lv, cv, lv_idx, cv_idx, False)


    def libman_get_view_and_index(self, cell_view):
        num_views = _mw.views()
        for lv_idx in range(num_views):
            lv = _mw.view(lv_idx)
            cv_idx = self.libman_cellview_index(lv, cell_view)
            if cv_idx != -1:
                return (lv, cv_idx)
        return (None, -1)


    def libman_cellview_index(self, layout_view, cell_view):
        n = layout_view.cellviews()
        for i in range(n):
            cv = layout_view.cellview(i)
            if cv == cell_view:
                return i
        return -1


    def libman_find_view_for_file(self, file_name):
        num_views = _mw.views()
        for lv_idx in range(num_views):
            lv = _mw.view(lv_idx)
            (cv, cv_idx) = self.libman_find_cellview(lv, file_name)
            if cv_idx != -1:
                return (lv, cv, lv_idx, cv_idx)
        return (None, None, -1, -1)


    def libman_find_cellview(self, layout_view, file_name):
        n = layout_view.cellviews()
        for i in range(n):
            cv = layout_view.cellview(i)
            fn = cv.filename()
            if libman_cmp_paths(fn, file_name):
                return (cv, i)
        return (None, -1)


#==============================================================================
# Call
req = LibManRequest()
)";

    out << "req.open_cell(" << pyRawString(gdsPath) << ", " << pyRawString(cellName) << ")\n";
    out << "libman_fit_view_to_window()\n";

    out.flush();
    tf.close();

    return tf.fileName();
}

QString KLayoutTools::createOpenWithOverlayScript(const QString &masterPath,
                                                  const QString &masterCell,
                                                  const QString &overlayPath,
                                                  const QString &overlayCell) const
{
    // Reuse the single-open script body, then open master + overlay into the same view.
    QTemporaryFile tf(QDir::tempPath() + QDir::separator() + "libman_klayout_open_overlay_XXXXXX.py");
    tf.setAutoRemove(false);

    if(!tf.open()) {
        return QString();
    }

    // Base helpers/classes from createOpenScript without the final open_cell call.
    const QString baseScript = createOpenScript(masterPath, masterCell);
    if(baseScript.isEmpty()) {
        return QString();
    }

    QFile base(baseScript);
    if(!base.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QFile::remove(baseScript);
        return QString();
    }
    QByteArray body = base.readAll();
    base.close();
    QFile::remove(baseScript);

    // Drop the trailing single open_cell / fit calls; append dual open.
    const int cut = body.lastIndexOf("req.open_cell(");
    if(cut >= 0) {
        body = body.left(cut);
    }

    QTextStream out(&tf);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#else
    out.setEncoding(QStringConverter::Utf8);
#endif
    out << QString::fromUtf8(body);
    out << "req.open_cell(" << pyRawString(masterPath) << ", " << pyRawString(masterCell) << ")\n";
    out << "req.open_cell(" << pyRawString(overlayPath) << ", " << pyRawString(overlayCell) << ")\n";
    out << "libman_fit_view_to_window()\n";
    out.flush();
    tf.close();
    return tf.fileName();
}

QString KLayoutTools::createXorScript(const QString &pathA,
                                      const QString &cellA,
                                      const QString &pathB,
                                      const QString &cellB,
                                      const QString &outputPath) const
{
    QTemporaryFile tf(QDir::tempPath() + QDir::separator() + "libman_klayout_xor_XXXXXX.py");
    tf.setAutoRemove(false);

    if(!tf.open()) {
        return QString();
    }

    QTextStream out(&tf);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#else
    out.setEncoding(QStringConverter::Utf8);
#endif

    out <<
        R"LIBMAN_XOR(# -*- coding: utf-8 -*-
# LibMan layout XOR (batch)
# Output GDS contains:
#   MASTER/<original>  — copy of layout A (first XOR operand)
#   XOR_DIFF           — shapes that differ
#   COMPARE            — both instanced together (open this cell)
import pya
import sys
import os

def libman_pick_cell(ly, preferred):
    if preferred:
        try:
            c = ly.cell(preferred)
            if c is not None:
                return c
        except Exception:
            pass
    tops = ly.top_cells()
    if tops:
        return tops[0]
    return None

def libman_layer_key(ly, li):
    info = ly.get_info(li)
    return (info.layer, info.datatype, info.name)

def libman_read_layout(ly, path):
    """Read GDS/OAS/LStream/ROOM (ROOM needs the mroom streamer in this KLayout build)."""
    if not os.path.isfile(path):
        print("ERROR: file not found: %s" % path)
        return False
    try:
        ly.read(path)
        return True
    except Exception as e:
        print("ERROR: failed to read layout: %s" % path)
        print("  %s" % e)
        if path.lower().endswith(".room"):
            print("HINT: *.layout.room requires the mroom streamer (KLayout-roomdb).")
            print("      Rebuild KLayout with integrations/klayout/mroom and use that klayout.exe.")
        return False

def libman_xor(path_a, cell_a, path_b, cell_b, output_path):
    print("LibMan XOR")
    print("  A: %s" % path_a)
    print("  B: %s" % path_b)

    lya = pya.Layout()
    lyb = pya.Layout()
    if not libman_read_layout(lya, path_a):
        return 2
    if not libman_read_layout(lyb, path_b):
        return 2

    ca = libman_pick_cell(lya, cell_a)
    cb = libman_pick_cell(lyb, cell_b)
    if ca is None or cb is None:
        print("ERROR: could not resolve top/cell for XOR")
        return 2

    print("  cell A: %s" % ca.name)
    print("  cell B: %s" % cb.name)

    layer_map = {}
    for li in lya.layer_indices():
        layer_map[libman_layer_key(lya, li)] = True
    for li in lyb.layer_indices():
        layer_map[libman_layer_key(lyb, li)] = True

    keys = sorted(layer_map.keys(), key=lambda k: (k[0], k[1], k[2] or ""))
    total = 0
    differing = 0
    xor_by_layer = []

    for (layer, datatype, name) in keys:
        info = pya.LayerInfo(layer, datatype)
        if name:
            info.name = name
        lia = lya.layer(info)
        lib = lyb.layer(info)
        ra = pya.Region(ca.begin_shapes_rec(lia))
        rb = pya.Region(cb.begin_shapes_rec(lib))
        xor = ra ^ rb
        n = xor.count()
        label = "%s/%s" % (layer, datatype)
        if name:
            label = "%s (%s)" % (label, name)
        print("  layer %s: %d XOR shape(s)" % (label, n))
        total += n
        if n > 0:
            differing += 1
            xor_by_layer.append((info, xor))

    print("XOR summary: %d differing layer(s), %d total XOR shape(s)" % (differing, total))
    if total == 0:
        return 0

    # Combined output: master A + XOR_DIFF under COMPARE (one file, one window).
    ly_out = pya.Layout()
    ly_out.dbu = lya.dbu
    ly_out.read(path_a)

    master_top = libman_pick_cell(ly_out, cell_a)
    if master_top is None:
        print("ERROR: could not resolve master cell in output layout")
        return 2

    master_name = master_top.name
    if master_name in ("COMPARE", "XOR_DIFF", "XOR"):
        # Avoid name clash with our synthetic cells.
        ly_out.rename_cell(master_top.cell_index(), "MASTER")
        master_top = ly_out.cell("MASTER")

    xor_idx = ly_out.add_cell("XOR_DIFF")
    for (info, xor) in xor_by_layer:
        li_out = ly_out.layer(info)
        xor.insert_into(ly_out, xor_idx, li_out)

    compare_idx = ly_out.add_cell("COMPARE")
    compare = ly_out.cell(compare_idx)
    compare.insert(pya.CellInstArray(master_top.cell_index(), pya.Trans()))
    compare.insert(pya.CellInstArray(xor_idx, pya.Trans()))

    ly_out.write(output_path)
    print("  wrote COMPARE (master + XOR_DIFF): %s" % output_path)
    return 1

)LIBMAN_XOR";

    out << "sys.exit(libman_xor("
        << pyRawString(pathA) << ", " << pyRawString(cellA) << ", "
        << pyRawString(pathB) << ", " << pyRawString(cellB) << ", "
        << pyRawString(outputPath) << "))\n";

    out.flush();
    tf.close();
    return tf.fileName();
}

void KLayoutTools::logXorProcessOutput(const QString &text)
{
    if(text.isEmpty()) {
        return;
    }

    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")),
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
                                          Qt::SkipEmptyParts);
#else
                                          Qt::SkipEmptyParts);
#endif
    for(const QString &line : lines) {
        if(isKLayoutXorStartupNoise(line)) {
            continue;
        }
        emit info(line);
    }
}

void KLayoutTools::startXorProcess(const QString &program,
                                   const QStringList &args,
                                   const QString &scriptPath,
                                   const QString &outputPath,
                                   const QString &masterPath,
                                   const QString &masterCell)
{
    QProcess *p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);

    connect(p, &QProcess::readyReadStandardOutput, this, [this, p]() {
        logXorProcessOutput(QString::fromLocal8Bit(p->readAllStandardOutput()));
    });

    connect(p,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, p, scriptPath, outputPath, masterPath, masterCell](int exitCode, QProcess::ExitStatus status) {
                logXorProcessOutput(QString::fromLocal8Bit(p->readAllStandardOutput()));

                if(status != QProcess::NormalExit) {
                    emit error(QObject::tr("\nXOR: KLayout process crashed."));
                }
                else if(exitCode == 0) {
                    emit info(QObject::tr("XOR: layouts are identical (no XOR shapes)."));
                }
                else if(exitCode == 1) {
                    emit info(QObject::tr("XOR: differences found (see log above)."));
                    if(QFileInfo::exists(outputPath)) {
                        if(!masterPath.isEmpty()) {
                            QJsonObject meta;
                            meta.insert(QStringLiteral("master"),
                                        QFileInfo(masterPath).absoluteFilePath());
                            meta.insert(QStringLiteral("cell"), masterCell);
                            meta.insert(QStringLiteral("overlay_cell"), QStringLiteral("XOR"));
                            QFile metaFile(outputPath + QStringLiteral(".meta.json"));
                            if(metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                                metaFile.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
                                metaFile.close();
                            }
                        }
                        emit fileLink(outputPath, outputPath);
                    }
                }
                else {
                    emit error(QObject::tr("\nXOR: KLayout exited with code %1.").arg(exitCode));
                    if(QFileInfo::exists(outputPath)) {
                        emit fileLink(outputPath, outputPath);
                    }
                }

                if(!scriptPath.isEmpty() && QFileInfo::exists(scriptPath)) {
                    QFile::remove(scriptPath);
                }
                p->deleteLater();
            });

    connect(p, &QProcess::errorOccurred, this, [this, p, scriptPath](QProcess::ProcessError) {
        emit error(QObject::tr("\nXOR: failed to start KLayout (%1).").arg(p->errorString()));
        if(!scriptPath.isEmpty() && QFileInfo::exists(scriptPath)) {
            QFile::remove(scriptPath);
        }
        p->deleteLater();
    });

    p->start(program, args);
}

bool KLayoutTools::readXorMeta(const QString &xorOutputPath,
                               QString *masterPath,
                               QString *masterCell)
{
    if(masterPath) {
        masterPath->clear();
    }
    if(masterCell) {
        masterCell->clear();
    }

    const QString metaPath = xorOutputPath + QStringLiteral(".meta.json");
    QFile f(metaPath);
    if(!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if(!doc.isObject()) {
        return false;
    }

    const QJsonObject obj = doc.object();
    const QString master = obj.value(QStringLiteral("master")).toString().trimmed();
    if(master.isEmpty() || !QFileInfo::exists(master)) {
        return false;
    }

    if(masterPath) {
        *masterPath = master;
    }
    if(masterCell) {
        *masterCell = obj.value(QStringLiteral("cell")).toString();
    }
    return true;
}

void KLayoutTools::startToolWithTempScript(const QString &tool,
                                           const QStringList &args,
                                           const QString &scriptPath)
{
    QProcess *p = new QProcess(this);

    connect(p,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [p, scriptPath](int, QProcess::ExitStatus) {
                if(!scriptPath.isEmpty() && QFileInfo::exists(scriptPath)) {
                    QFile::remove(scriptPath);
                }
                p->deleteLater();
            });

    connect(p,
            &QProcess::errorOccurred,
            this,
            [scriptPath](QProcess::ProcessError) {
                if(!scriptPath.isEmpty() && QFileInfo::exists(scriptPath)) {
                    QFile::remove(scriptPath);
                }
            });

    p->start(tool, args);
}

