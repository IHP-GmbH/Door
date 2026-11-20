#include "python_console.h"
#include "room_python_path.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace {

constexpr const char kDoneSentinel[] = "@@LIBMAN_DONE@@";
constexpr const char kJsonPrefix[] = "@@LIBMAN_JSON@@";

// Fallback only if python/libman_console.py cannot be found (dev misconfig).
const char kEmbeddedDriverFallback[] = R"PY(
import sys, traceback, json, os
g = {"__name__": "__main__"}
try:
    import room
    g["room"] = room
except Exception as e:
    print("WARNING: import room failed: %s" % e, file=sys.stderr)

def load(path):
    exec(open(path, encoding="utf-8-sig").read(), g)

def _emit(cmd, **payload):
    msg = {"cmd": cmd}
    msg.update(payload)
    sys.stdout.write("@@LIBMAN_JSON@@" + json.dumps(msg, ensure_ascii=False) + "\n")
    sys.stdout.flush()

def define(library, path):
    abs_path = os.path.abspath(os.path.expanduser(str(path)))
    _emit("define", library=str(library), path=abs_path)
    return abs_path

def undefine(library, path):
    abs_path = os.path.abspath(os.path.expanduser(str(path)))
    _emit("undefine", library=str(library), path=abs_path)
    return abs_path

g["load"] = load
g["define"] = define
g["undefine"] = undefine
sys.stdout.flush(); sys.stderr.flush()
while True:
    header = sys.stdin.readline()
    if not header: break
    header = header.strip()
    if not header: continue
    try: nbytes = int(header)
    except ValueError:
        print("ERROR: bad command header: %r" % (header,), file=sys.stderr)
        print("@@LIBMAN_DONE@@"); sys.stdout.flush(); sys.stderr.flush(); continue
    buf = sys.stdin.read(nbytes)
    if len(buf) < nbytes: break
    src = buf if isinstance(buf, str) else buf.decode("utf-8", "replace")
    try:
        try: code = compile(src, "<libman>", "eval")
        except SyntaxError: code = None
        if code is not None:
            result = eval(code, g, g)
            if result is not None: print(repr(result))
        else:
            exec(compile(src, "<libman>", "exec"), g, g)
    except SystemExit: raise
    except BaseException: traceback.print_exc()
    print("@@LIBMAN_DONE@@"); sys.stdout.flush(); sys.stderr.flush()
)PY";

QStringList uniqueExistingDirs(const QStringList &candidates)
{
    QStringList out;
    for (const QString &path : candidates) {
        if (path.isEmpty()) {
            continue;
        }
        const QFileInfo fi(path);
        if (!fi.exists() || !fi.isDir()) {
            continue;
        }
        const QString abs = fi.absoluteFilePath();
        if (!out.contains(abs)) {
            out.append(abs);
        }
    }
    return out;
}

} // namespace

PythonConsole::PythonConsole(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, &PythonConsole::onReadyRead);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &PythonConsole::onProcessFinished);
}

PythonConsole::~PythonConsole()
{
    stop();
}

bool PythonConsole::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool PythonConsole::isBusy() const
{
    return m_busy;
}

QString PythonConsole::findPythonExecutable() const
{
    for (const char *key : {"DOOR_PYTHON", "LIBMAN_PYTHON"}) {
        const QString fromEnv = qEnvironmentVariable(key).trimmed();
        if (!fromEnv.isEmpty() && QFileInfo::exists(fromEnv)) {
            return QFileInfo(fromEnv).absoluteFilePath();
        }
    }

    const QStringList names = {
        QStringLiteral("python"),
        QStringLiteral("python3"),
#ifdef Q_OS_WIN
        QStringLiteral("python.exe"),
        QStringLiteral("python3.exe"),
#endif
    };
    for (const QString &name : names) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) {
            return found;
        }
    }

#ifdef Q_OS_WIN
    const QStringList winCandidates = {
        QDir::home().filePath(QStringLiteral("AppData/Local/Programs/Python/Python311/python.exe")),
        QDir::home().filePath(QStringLiteral("AppData/Local/Programs/Python/Python312/python.exe")),
        QDir::home().filePath(QStringLiteral("AppData/Local/Programs/Python/Python313/python.exe")),
    };
    for (const QString &path : winCandidates) {
        if (QFileInfo::exists(path)) {
            return QFileInfo(path).absoluteFilePath();
        }
    }
#endif
    return {};
}

QString PythonConsole::findRoomPackageDir() const
{
    return resolveRoomPackageDir();
}

QString PythonConsole::findRoomCLibrary() const
{
    const QString env = qEnvironmentVariable("ROOM_C_DLL").trimmed();
    if (!env.isEmpty() && QFileInfo::exists(env)) {
        return QFileInfo(env).absoluteFilePath();
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList names;
#ifdef Q_OS_WIN
    names << QStringLiteral("room_c.dll") << QStringLiteral("libroom_c.dll");
#elif defined(Q_OS_MACOS)
    names << QStringLiteral("libroom_c.dylib") << QStringLiteral("room_c.dylib");
#else
    names << QStringLiteral("libroom_c.so") << QStringLiteral("room_c.so");
#endif

    QStringList dirs;
    dirs << appDir << QDir(appDir).filePath(QStringLiteral("python"));

    // Prefer DLL next to the configured Room package / checkout.
    // pkg is the dir that contains room/__init__.py (…/python); room_c often
    // lives inside the package folder itself (…/python/room/).
    const QString pkg = findRoomPackageDir();
    if (!pkg.isEmpty()) {
        dirs << pkg;
        dirs << QDir(pkg).filePath(QStringLiteral("room"));
        dirs << QDir(pkg).filePath(QStringLiteral("../build-python/python"));
        dirs << QDir(pkg).filePath(QStringLiteral("../build/python"));
        dirs << QDir(pkg).filePath(QStringLiteral("build-python/python"));
        dirs << QDir(pkg).filePath(QStringLiteral("build/python"));
    }

    QDir walk(appDir);
    for (int i = 0; i < 8; ++i) {
        for (const char *build : {"build-python/python", "build/python"}) {
            const QString leaf = QStringLiteral("Room/") + QString::fromUtf8(build);
            dirs << walk.filePath(leaf);
            dirs << QFileInfo(walk.filePath(QStringLiteral("../") + leaf)).absoluteFilePath();
        }
        dirs << walk.filePath(QStringLiteral(".deps/room-build/python"));
        if (!walk.cdUp()) {
            break;
        }
    }

    for (const QString &dir : uniqueExistingDirs(dirs)) {
        for (const QString &name : names) {
            const QString candidate = QDir(dir).filePath(name);
            if (QFileInfo::exists(candidate)) {
                return QFileInfo(candidate).absoluteFilePath();
            }
        }
    }
    return {};
}

QString PythonConsole::findDriverScript() const
{
    for (const char *key : {"DOOR_CONSOLE_PY", "LIBMAN_CONSOLE_PY"}) {
        const QString env = qEnvironmentVariable(key).trimmed();
        if (!env.isEmpty() && QFileInfo::exists(env)) {
            return QFileInfo(env).absoluteFilePath();
        }
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("python/door_console.py")),
        QDir(appDir).filePath(QStringLiteral("python/libman_console.py")),
        QDir(appDir).filePath(QStringLiteral("../python/door_console.py")),
        QDir(appDir).filePath(QStringLiteral("../python/libman_console.py")),
        QDir(appDir).filePath(QStringLiteral("../../python/door_console.py")),
        QDir(appDir).filePath(QStringLiteral("../../python/libman_console.py")),
        QDir(appDir).filePath(QStringLiteral("../../../python/door_console.py")),
        QDir(appDir).filePath(QStringLiteral("../../../python/libman_console.py")),
        // cmake build/ next to source tree
        QDir(appDir).filePath(QStringLiteral("../../Door/python/door_console.py")),
        QDir(appDir).filePath(QStringLiteral("../../Door/python/libman_console.py")),
        QDir(appDir).filePath(QStringLiteral("../../LibMan/python/door_console.py")),
        QDir(appDir).filePath(QStringLiteral("../../LibMan/python/libman_console.py")),
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path)) {
            return QFileInfo(path).absoluteFilePath();
        }
    }
    return {};
}

QString PythonConsole::writeEmbeddedDriverFallback() const
{
    QTemporaryFile file(QDir::temp().filePath(QStringLiteral("libman_py_XXXXXX.py")));
    file.setAutoRemove(false);
    if (!file.open()) {
        return {};
    }
    if (file.write(kEmbeddedDriverFallback) < 0) {
        return {};
    }
    file.flush();
    const QString path = file.fileName();
    file.close();
    return path;
}

void PythonConsole::start()
{
    if (isRunning()) {
        return;
    }

    const QString python = findPythonExecutable();
    if (python.isEmpty()) {
        emit failed(tr("Python interpreter not found. Set DOOR_PYTHON or LIBMAN_PYTHON, or install Python on PATH."));
        return;
    }

    QString driver = findDriverScript();
    if (driver.isEmpty()) {
        driver = writeEmbeddedDriverFallback();
        if (driver.isEmpty()) {
            emit failed(tr("Failed to locate python/door_console.py and could not write fallback."));
            return;
        }
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString roomPkg = findRoomPackageDir();
    if (!roomPkg.isEmpty()) {
        const QString existing = env.value(QStringLiteral("PYTHONPATH"));
        env.insert(QStringLiteral("PYTHONPATH"),
                   existing.isEmpty() ? roomPkg : (roomPkg + QDir::listSeparator() + existing));
    }
    const QString roomC = findRoomCLibrary();
    if (!roomC.isEmpty()) {
        env.insert(QStringLiteral("ROOM_C_DLL"), roomC);
    }

    m_process->setProcessEnvironment(env);
    m_process->setProgram(python);
    m_process->setArguments({QStringLiteral("-u"), driver});
    m_stdoutBuffer.clear();
    m_lineBuffer.clear();
    m_busy = false;
    m_process->start();

    if (!m_process->waitForStarted(10000)) {
        emit failed(tr("Failed to start Python: %1").arg(m_process->errorString()));
        return;
    }

    QString detail = tr("Python: %1").arg(QDir::toNativeSeparators(python));
    detail += tr("\nDriver: %1").arg(QDir::toNativeSeparators(driver));
    if (!roomPkg.isEmpty()) {
        detail += tr("\nPYTHONPATH: %1").arg(QDir::toNativeSeparators(roomPkg));
    }
    if (!roomC.isEmpty()) {
        detail += tr("\nROOM_C_DLL: %1").arg(QDir::toNativeSeparators(roomC));
    }
    emit started(detail);
}

void PythonConsole::stop()
{
    if (!m_process) {
        return;
    }
    if (m_process->state() != QProcess::NotRunning) {
        m_process->write("0\n");
        m_process->closeWriteChannel();
        if (!m_process->waitForFinished(1500)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
    m_busy = false;
}

void PythonConsole::execute(const QString &code)
{
    if (!isRunning()) {
        start();
    }
    if (!isRunning()) {
        emit commandFinished(false);
        return;
    }
    if (m_busy) {
        emit output(tr("Python console is busy; wait for the previous command to finish."));
        emit commandFinished(false);
        return;
    }

    QString trimmed = code;
    while (trimmed.endsWith(QLatin1Char('\n')) || trimmed.endsWith(QLatin1Char('\r'))) {
        trimmed.chop(1);
    }
    if (trimmed.trimmed().isEmpty()) {
        emit commandFinished(true);
        return;
    }

    const QByteArray payload = trimmed.toUtf8();
    m_busy = true;
    const QByteArray header = QByteArray::number(payload.size()) + '\n';
    if (m_process->write(header) < 0 || m_process->write(payload) < 0) {
        m_busy = false;
        emit output(tr("Failed to write command to Python process."));
        emit commandFinished(false);
        return;
    }
}

void PythonConsole::onReadyRead()
{
    m_stdoutBuffer += m_process->readAll();
    emitPendingOutput();
}

void PythonConsole::handleOutputChunk(const QString &chunk)
{
    m_lineBuffer += chunk;
    int nl = m_lineBuffer.indexOf(QLatin1Char('\n'));
    while (nl >= 0) {
        QString line = m_lineBuffer.left(nl);
        m_lineBuffer.remove(0, nl + 1);
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }

        if (line.startsWith(QLatin1String(kJsonPrefix))) {
            const QByteArray jsonBytes = line.mid(int(sizeof(kJsonPrefix) - 1)).toUtf8();
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                emit libmanCommand(doc.object());
            } else {
                emit output(tr("Bad DOOR CML command JSON: %1").arg(err.errorString()));
            }
        } else if (!line.isEmpty()) {
            emit output(line + QLatin1Char('\n'));
        }

        nl = m_lineBuffer.indexOf(QLatin1Char('\n'));
    }
}

void PythonConsole::emitPendingOutput()
{
    while (true) {
        const int idx = m_stdoutBuffer.indexOf(kDoneSentinel);
        if (idx < 0) {
            const int lastNl = m_stdoutBuffer.lastIndexOf('\n');
            if (lastNl < 0) {
                return;
            }
            const QByteArray chunk = m_stdoutBuffer.left(lastNl + 1);
            m_stdoutBuffer.remove(0, lastNl + 1);
            handleOutputChunk(QString::fromUtf8(chunk));
            return;
        }

        const QByteArray before = m_stdoutBuffer.left(idx);
        m_stdoutBuffer.remove(0, idx + int(sizeof(kDoneSentinel) - 1));
        if (m_stdoutBuffer.startsWith('\r')) {
            m_stdoutBuffer.remove(0, 1);
        }
        if (m_stdoutBuffer.startsWith('\n')) {
            m_stdoutBuffer.remove(0, 1);
        }

        handleOutputChunk(QString::fromUtf8(before));
        // Flush any incomplete non-JSON line left in m_lineBuffer before finishing.
        if (!m_lineBuffer.isEmpty()) {
            handleOutputChunk(QStringLiteral("\n"));
        }
        m_busy = false;
        emit commandFinished(true);
    }
}

void PythonConsole::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    emitPendingOutput();
    if (!m_stdoutBuffer.isEmpty()) {
        handleOutputChunk(QString::fromUtf8(m_stdoutBuffer));
        m_stdoutBuffer.clear();
    }
    if (!m_lineBuffer.isEmpty()) {
        emit output(m_lineBuffer);
        m_lineBuffer.clear();
    }
    m_busy = false;
    if (status != QProcess::NormalExit || exitCode != 0) {
        emit failed(tr("Python console exited (code %1).").arg(exitCode));
    }
    emit commandFinished(false);
}
