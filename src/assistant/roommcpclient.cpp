#include "roommcpclient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

RoomMcpClient::RoomMcpClient(QObject *parent)
    : QObject(parent)
{
}

RoomMcpClient::~RoomMcpClient()
{
    stop();
}

void RoomMcpClient::stop()
{
    m_ready = false;
    if (!m_proc) {
        return;
    }
    m_proc->kill();
    m_proc->waitForFinished(2000);
    m_proc->deleteLater();
    m_proc = nullptr;
}

QString RoomMcpClient::findPython() const
{
    const QStringList names = {
#ifdef Q_OS_WIN
        QStringLiteral("python.exe"),
        QStringLiteral("python3.exe"),
#else
        QStringLiteral("python3"),
        QStringLiteral("python"),
#endif
    };
    for (const QString &n : names) {
        const QString p = QStandardPaths::findExecutable(n);
        if (!p.isEmpty()) {
            return p;
        }
    }
    return {};
}

QString RoomMcpClient::findRoomPackageDir() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("../CommonDB/python")),
        QDir(appDir).filePath(QStringLiteral("../../CommonDB/python")),
        QDir(appDir).filePath(QStringLiteral("../../../CommonDB/python")),
        QDir::cleanPath(QDir(appDir).filePath(QStringLiteral("../python"))),
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(QDir(c).filePath(QStringLiteral("room/__init__.py")))) {
            return QDir(c).absolutePath();
        }
        if (QFileInfo::exists(QDir(c).filePath(QStringLiteral("room/mcp_server.py")))) {
            return QDir(c).absolutePath();
        }
    }
    // Same search as PythonConsole when ROOM is a sibling of LibMan.
    QDir d(appDir);
    for (int i = 0; i < 6; ++i) {
        const QString sib = d.absoluteFilePath(QStringLiteral("../CommonDB/python"));
        if (QFileInfo::exists(QDir(sib).filePath(QStringLiteral("room/__init__.py")))) {
            return QDir(sib).absolutePath();
        }
        if (!d.cdUp()) {
            break;
        }
    }
    return {};
}

QString RoomMcpClient::findRoomCLibrary() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList names = {
#ifdef Q_OS_WIN
        QStringLiteral("libroom_c.dll"),
        QStringLiteral("room_c.dll"),
#else
        QStringLiteral("libroom_c.so"),
        QStringLiteral("room_c.so"),
#endif
    };
    const QStringList dirs = {
        appDir,
        QDir(appDir).filePath(QStringLiteral("python")),
        QDir(appDir).filePath(QStringLiteral("../CommonDB/build/python")),
        QDir(appDir).filePath(QStringLiteral("../../CommonDB/build/python")),
    };
    for (const QString &dir : dirs) {
        for (const QString &n : names) {
            const QString p = QDir(dir).filePath(n);
            if (QFileInfo::exists(p)) {
                return QFileInfo(p).absoluteFilePath();
            }
        }
    }
    return {};
}

bool RoomMcpClient::ensureStarted()
{
    if (m_ready && m_proc && m_proc->state() == QProcess::Running) {
        return true;
    }
    stop();

    const QString python = findPython();
    const QString pkg = findRoomPackageDir();
    if (python.isEmpty() || pkg.isEmpty()) {
        return false;
    }

    m_proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString existing = env.value(QStringLiteral("PYTHONPATH"));
    env.insert(QStringLiteral("PYTHONPATH"),
               existing.isEmpty() ? pkg : (pkg + QDir::listSeparator() + existing));
    const QString dll = findRoomCLibrary();
    if (!dll.isEmpty()) {
        env.insert(QStringLiteral("ROOM_C_DLL"), dll);
    }
    m_proc->setProcessEnvironment(env);
    m_proc->setProgram(python);
    m_proc->setArguments({QStringLiteral("-m"), QStringLiteral("room.mcp_server")});
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);
    m_proc->start();
    if (!m_proc->waitForStarted(5000)) {
        stop();
        return false;
    }

    const QJsonObject init = request(QStringLiteral("initialize"), QJsonObject{
        {QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05")},
        {QStringLiteral("capabilities"), QJsonObject()},
        {QStringLiteral("clientInfo"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("door")},
                     {QStringLiteral("version"), QStringLiteral("1.0")}}},
    });
    if (init.isEmpty() || init.contains(QStringLiteral("error"))) {
        stop();
        return false;
    }

    // notifications/initialized (no response expected)
    {
        QJsonObject note;
        note.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        note.insert(QStringLiteral("method"), QStringLiteral("notifications/initialized"));
        const QByteArray line = QJsonDocument(note).toJson(QJsonDocument::Compact) + '\n';
        m_proc->write(line);
        m_proc->waitForBytesWritten(2000);
    }

    m_ready = true;
    return true;
}

QJsonObject RoomMcpClient::request(const QString &method, const QJsonObject &params)
{
    if (!m_proc || m_proc->state() != QProcess::Running) {
        return {{QStringLiteral("error"), QStringLiteral("MCP process not running")}};
    }

    const int id = m_nextId++;
    QJsonObject req;
    req.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    req.insert(QStringLiteral("id"), id);
    req.insert(QStringLiteral("method"), method);
    if (!params.isEmpty()) {
        req.insert(QStringLiteral("params"), params);
    }

    const QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n';
    m_proc->write(line);
    if (!m_proc->waitForBytesWritten(5000)) {
        return {{QStringLiteral("error"), QStringLiteral("Failed to write MCP request")}};
    }

    // Read until we get a JSON line with matching id.
    QByteArray buf;
    const int maxWaitMs = 60000;
    int waited = 0;
    while (waited < maxWaitMs) {
        if (m_proc->waitForReadyRead(500)) {
            buf += m_proc->readAllStandardOutput();
            // Surface stderr (tool tracebacks) without blocking the reply.
            const QByteArray errChunk = m_proc->readAllStandardError();
            Q_UNUSED(errChunk);
            while (true) {
                const int nl = buf.indexOf('\n');
                if (nl < 0) {
                    break;
                }
                const QByteArray one = buf.left(nl).trimmed();
                buf.remove(0, nl + 1);
                if (one.isEmpty()) {
                    continue;
                }
                const QJsonDocument doc = QJsonDocument::fromJson(one);
                if (!doc.isObject()) {
                    continue;
                }
                const QJsonObject obj = doc.object();
                if (obj.value(QStringLiteral("id")).toInt() != id) {
                    continue;
                }
                if (obj.contains(QStringLiteral("error"))) {
                    const QJsonValue err = obj.value(QStringLiteral("error"));
                    QJsonObject out;
                    out.insert(QStringLiteral("ok"), false);
                    out.insert(QStringLiteral("error"),
                               err.isObject() ? err.toObject().value(QStringLiteral("message")).toString()
                                              : err.toString());
                    return out;
                }
                return obj.value(QStringLiteral("result")).toObject();
            }
        } else {
            waited += 500;
            if (m_proc->state() != QProcess::Running) {
                m_ready = false;
                return {{QStringLiteral("ok"), false},
                        {QStringLiteral("error"), QStringLiteral("MCP process exited")}};
            }
        }
    }
    // Stale process after timeout — force restart on next call.
    m_ready = false;
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("MCP request timed out")}};
}

QJsonArray RoomMcpClient::listTools()
{
    if (!ensureStarted()) {
        return {};
    }
    const QJsonObject res = request(QStringLiteral("tools/list"));
    return res.value(QStringLiteral("tools")).toArray();
}

QJsonObject RoomMcpClient::callTool(const QString &name, const QJsonObject &args)
{
    if (!ensureStarted()) {
        // One retry after a previous crash/timeout.
        stop();
        if (!ensureStarted()) {
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"),
                     QStringLiteral("ROOM MCP not available (python -m room.mcp_server)")}};
        }
    }
    QJsonObject params;
    params.insert(QStringLiteral("name"), name);
    params.insert(QStringLiteral("arguments"), args);
    QJsonObject res = request(QStringLiteral("tools/call"), params);
    if (res.value(QStringLiteral("error")).toString().contains(QStringLiteral("timed out"))
        || res.value(QStringLiteral("error")).toString().contains(QStringLiteral("exited"))) {
        stop();
        if (ensureStarted()) {
            res = request(QStringLiteral("tools/call"), params);
        }
    }
    // Normalize MCP content → ok/text for AssistantMcp formatting.
    if (res.contains(QStringLiteral("content"))) {
        QString text;
        const QJsonArray content = res.value(QStringLiteral("content")).toArray();
        for (const QJsonValue &v : content) {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("type")).toString() == QLatin1String("text")) {
                if (!text.isEmpty()) {
                    text += QLatin1Char('\n');
                }
                text += o.value(QStringLiteral("text")).toString();
            }
        }
        QJsonObject out;
        out.insert(QStringLiteral("ok"), !res.value(QStringLiteral("isError")).toBool(false));
        out.insert(QStringLiteral("text"), text);
        if (res.value(QStringLiteral("isError")).toBool(false)) {
            out.insert(QStringLiteral("error"), text);
        }
        return out;
    }
    if (!res.contains(QStringLiteral("ok"))) {
        res.insert(QStringLiteral("ok"), !res.contains(QStringLiteral("error")));
    }
    return res;
}
