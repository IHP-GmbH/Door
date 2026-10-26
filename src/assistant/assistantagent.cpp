/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 ************************************************************************/

#include "assistantagent.h"
#include "assistantmcp.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>

/*!*******************************************************************************************************************
 * \brief Constructs the LLM agent and its network access manager.
 *
 * \param parent Optional QObject parent.
 **********************************************************************************************************************/
AssistantAgent::AssistantAgent(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

/*!*******************************************************************************************************************
 * \brief Attaches the MCP tool registry used for function calling.
 *
 * \param mcp In-process tool registry (may be null).
 **********************************************************************************************************************/
void AssistantAgent::setMcp(AssistantMcp *mcp)
{
    m_mcp = mcp;
}

/*!*******************************************************************************************************************
 * \brief Applies LLM endpoint settings (API key, base URL, model id).
 *
 * \param apiKey  Bearer token (optional for local servers).
 * \param baseUrl Chat Completions base URL.
 * \param model   Model id.
 **********************************************************************************************************************/
void AssistantAgent::configure(const QString &apiKey, const QString &baseUrl, const QString &model)
{
    m_apiKey = apiKey.trimmed();
    m_baseUrl = baseUrl.trimmed();
    m_model = model.trimmed();
    if (m_baseUrl.endsWith(QLatin1Char('/')))
        m_baseUrl.chop(1);
}

/*!*******************************************************************************************************************
 * \brief Returns whether the agent can send requests (URL + model; key for cloud hosts).
 **********************************************************************************************************************/
bool AssistantAgent::isConfigured() const
{
    if (m_model.isEmpty() || m_baseUrl.isEmpty())
        return false;

    const QString host = QUrl(m_baseUrl).host().toLower();
    const bool looksLocal = host.isEmpty()
                            || host == QLatin1String("localhost")
                            || host == QLatin1String("127.0.0.1")
                            || host.endsWith(QLatin1String(".local"));
    if (!looksLocal && m_apiKey.isEmpty())
        return false;
    return true;
}

/*!*******************************************************************************************************************
 * \brief Clears the chat completion message history.
 **********************************************************************************************************************/
void AssistantAgent::clearHistory()
{
    m_messages = QJsonArray();
}

/*!*******************************************************************************************************************
 * \brief Aborts an in-flight Qt Network or curl request and emits \c failed("Stopped.").
 **********************************************************************************************************************/
void AssistantAgent::cancel()
{
    if (!m_busy)
        return;

    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_curl) {
        disconnect(m_curl, nullptr, this, nullptr);
        m_curl->kill();
        m_curl->deleteLater();
        m_curl = nullptr;
    }
    if (!m_curlBodyPath.isEmpty()) {
        QFile::remove(m_curlBodyPath);
        m_curlBodyPath.clear();
    }
    m_round = 0;
    failWith(QStringLiteral("Stopped."));
}

/*!*******************************************************************************************************************
 * \brief Sends a user message and starts the LLM + tool-calling loop.
 *
 * \param userText User chat text.
 **********************************************************************************************************************/
void AssistantAgent::send(const QString &userText)
{
    if (m_busy)
        return;
    if (!isConfigured()) {
        failWith(QStringLiteral(
            "Assistant LLM is not configured.\n"
            "Preferences → Assistant:\n"
            "  ASSISTANT_BASE_URL  (e.g. https://api.openai.com/v1\n"
            "                       or http://localhost:11434/v1 for Ollama)\n"
            "  ASSISTANT_MODEL     (e.g. gpt-4o-mini or llama3.1)\n"
            "  ASSISTANT_API_KEY   (required for OpenAI; optional for local)"));
        return;
    }
    if (!m_mcp) {
        failWith(QStringLiteral("MCP tools are not connected."));
        return;
    }

    // Keep system prompt fresh (tool list may grow) as the first message.
    {
        QJsonObject sys;
        sys.insert(QStringLiteral("role"), QStringLiteral("system"));
        sys.insert(QStringLiteral("content"), buildSystemPrompt());

        QJsonArray next;
        next.append(sys);
        for (const QJsonValue &v : m_messages) {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("role")).toString() == QLatin1String("system"))
                continue;
            next.append(v);
        }
        m_messages = next;
    }

    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    user.insert(QStringLiteral("content"), userText);
    m_messages.append(user);

    m_busy = true;
    m_round = 0;
    emit statusChanged(QStringLiteral("Calling model…"));
    postNextCompletion();
}

/*!*******************************************************************************************************************
 * \brief Builds the system prompt (app map + registered MCP tool list).
 **********************************************************************************************************************/
QString AssistantAgent::buildSystemPrompt() const
{
    QStringList lines;
    lines << QStringLiteral(
        "You are the DOOR Assistant — a narrow helper for DOOR (library manager) and ROOM "
        "(*.room Python API / converters). Refuse unrelated topics (general coding, news, etc.).\n"
        "\n"
        "CRITICAL path / selection rules:\n"
        "- “selected / currently selected / current view” ALWAYS means the LIVE SELECTION "
        "block below (and/or a fresh get_app_state call). NEVER reuse a path pasted earlier "
        "in the chat — the user may have changed Project/Cell/View between messages.\n"
        "- room.open() ONLY accepts *.room. NEVER open project_file / *.projects.\n"
        "\n"
        "How to answer data questions (instances, pins, layers, …):\n"
        "1) Prefer LIVE SELECTION view_path + cell. If unsure, call get_app_state again.\n"
        "2) ROOM data/mutate: run_python with `import room` (full API = Room MCP).\n"
        "   If unsure of a name, call room_api_help or room_api_dir — never invent methods.\n"
        "   Print results so Messages shows >>> code + stdout.\n"
        "3) Copy selected view into another library cell folder: copy_selected_view "
        "(DOOR), not Database.create_cell alone.\n"
        "4) Chat: one short line only (“Done — see Messages.”). Never paste lists.\n"
        "\n"
        "ROOM API facts (do not invent):\n"
        "- Methods need (): db.cells(), cell.content(), cnt.instances(), cnt.pins().\n"
        "- Schematic devices = cnt.instances() (no .devices / .cells on content).\n"
        "- Instance: inst.name (label), inst.cell_name (master), inst.properties (read-only snapshot).\n"
        "- Mutate: cnt.set_instance_property_by_name('Vdd','value','1.4') then db.save() "
        "(path optional after open; do NOT assign inst.properties[...] alone).\n"
        "- Pin: pin.name, pin.dir. Cell: cell.name() — method with ().\n"
        "- Typical: db=room.open(view_path); cell=db.find_cell(name) or db.cells()[0]; "
        "cnt=cell.content(); for inst in cnt.instances(): print(inst.name, inst.cell_name)\n"
        "\n"
        "Name filters: “containing / with / named like X” → substring on "
        "inst.name and/or inst.cell_name (`'nmos' in s.lower()`). Use startswith "
        "ONLY if the user says “starts with” / “prefix”.\n"
        "\n"
        "Python 3 only (print(...), not print \"...\"). Never invent API keys.\n");

    // Snapshot current UI selection every turn so history cannot stale the path.
    if (m_mcp && m_mcp->hasTool(QStringLiteral("get_app_state"))) {
        const QJsonObject st = m_mcp->callTool(QStringLiteral("get_app_state"));
        lines << QStringLiteral("LIVE SELECTION (authoritative for this turn — ignore older paths in chat):");
        lines << QStringLiteral("  library=%1").arg(st.value(QStringLiteral("library")).toString());
        lines << QStringLiteral("  cell=%1").arg(st.value(QStringLiteral("cell")).toString());
        lines << QStringLiteral("  view=%1").arg(st.value(QStringLiteral("view")).toString());
        lines << QStringLiteral("  view_path=%1").arg(st.value(QStringLiteral("view_path")).toString());
        lines << QString();
    }

    lines << QStringLiteral("Registered tools:");
    if (m_mcp) {
        const QJsonArray manifest = m_mcp->toolsManifest();
        for (const QJsonValue &v : manifest) {
            const QJsonObject t = v.toObject();
            lines << QStringLiteral("- %1: %2")
                         .arg(t.value(QStringLiteral("name")).toString(),
                              t.value(QStringLiteral("description")).toString());
        }
    } else {
        lines << QStringLiteral("- (no tools registered)");
    }
    return lines.join(QLatin1Char('\n'));
}

/*!*******************************************************************************************************************
 * \brief Resolves the Chat Completions HTTP endpoint from \c ASSISTANT_BASE_URL.
 **********************************************************************************************************************/
QString AssistantAgent::chatCompletionsUrl() const
{
    QString base = m_baseUrl;
    if (base.endsWith(QLatin1String("/chat/completions"), Qt::CaseInsensitive))
        return base;
    if (!base.endsWith(QLatin1String("/v1"), Qt::CaseInsensitive)
        && !base.contains(QLatin1String("/v1/"), Qt::CaseInsensitive))
        base += QStringLiteral("/v1");
    return base + QStringLiteral("/chat/completions");
}

/*!*******************************************************************************************************************
 * \brief Returns whether Qt reports a working TLS/OpenSSL backend.
 **********************************************************************************************************************/
bool AssistantAgent::qtSslWorks()
{
    return QSslSocket::supportsSsl();
}

/*!*******************************************************************************************************************
 * \brief Locates the system \c curl executable used as an HTTPS fallback.
 **********************************************************************************************************************/
QString AssistantAgent::findCurl()
{
    const QString curl = QStandardPaths::findExecutable(QStringLiteral("curl"));
    if (!curl.isEmpty())
        return curl;
#ifdef Q_OS_WIN
    const QString sys = QStringLiteral("C:/Windows/System32/curl.exe");
    if (QFile::exists(sys))
        return sys;
#endif
    return {};
}

/*!*******************************************************************************************************************
 * \brief Converts the MCP tool registry into OpenAI \c tools function definitions.
 **********************************************************************************************************************/
QJsonArray AssistantAgent::openaiToolsManifest() const
{
    QJsonArray tools;
    if (!m_mcp)
        return tools;

    const QJsonArray manifest = m_mcp->toolsManifest();
    for (const QJsonValue &v : manifest) {
        const QJsonObject t = v.toObject();
        QJsonObject fn;
        fn.insert(QStringLiteral("name"), t.value(QStringLiteral("name")));
        fn.insert(QStringLiteral("description"), t.value(QStringLiteral("description")));
        QJsonObject params = t.value(QStringLiteral("inputSchema")).toObject();
        if (params.isEmpty()) {
            params.insert(QStringLiteral("type"), QStringLiteral("object"));
            params.insert(QStringLiteral("properties"), QJsonObject());
        }
        fn.insert(QStringLiteral("parameters"), params);

        QJsonObject tool;
        tool.insert(QStringLiteral("type"), QStringLiteral("function"));
        tool.insert(QStringLiteral("function"), fn);
        tools.append(tool);
    }
    return tools;
}

/*!*******************************************************************************************************************
 * \brief Posts the next chat-completions request (Qt Network or curl for HTTPS).
 **********************************************************************************************************************/
void AssistantAgent::postNextCompletion()
{
    if (m_round >= kMaxRounds) {
        failWith(QStringLiteral("Agent stopped: too many tool rounds (%1).").arg(kMaxRounds));
        return;
    }
    ++m_round;

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_model);
    body.insert(QStringLiteral("messages"), m_messages);
    const QJsonArray tools = openaiToolsManifest();
    if (!tools.isEmpty()) {
        body.insert(QStringLiteral("tools"), tools);
        body.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
    }

    const QByteArray bodyJson = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const QUrl url(chatCompletionsUrl());
    const bool needTls = url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0;

    if (needTls && (!qtSslWorks() || !findCurl().isEmpty())) {
        if (!findCurl().isEmpty()) {
            postViaCurl(bodyJson);
            return;
        }
    }

    postViaQt(bodyJson);
}

/*!*******************************************************************************************************************
 * \brief Sends the request body with \c QNetworkAccessManager.
 **********************************************************************************************************************/
void AssistantAgent::postViaQt(const QByteArray &bodyJson)
{
    QNetworkRequest req{QUrl(chatCompletionsUrl())};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_apiKey.isEmpty()) {
        req.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());
    }

    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    m_reply = m_nam->post(req, bodyJson);
    connect(m_reply, &QNetworkReply::finished, this, &AssistantAgent::onNetworkFinished);
}

/*!*******************************************************************************************************************
 * \brief Sends the request body via system curl (Schannel on Windows).
 **********************************************************************************************************************/
void AssistantAgent::postViaCurl(const QByteArray &bodyJson)
{
    const QString curl = findCurl();
    if (curl.isEmpty()) {
        failWith(QStringLiteral(
            "LLM HTTPS needs TLS, but Qt OpenSSL is missing and curl was not found.\n"
            "Fix one of:\n"
            "  • Install curl (Windows 10+ usually has it), or\n"
            "  • Use local Ollama over http://localhost:11434/v1 (no TLS), or\n"
            "  • Place libssl-1_1-x64.dll + libcrypto-1_1-x64.dll next to door.exe"));
        return;
    }

    if (m_curl) {
        m_curl->kill();
        m_curl->deleteLater();
        m_curl = nullptr;
    }
    if (!m_curlBodyPath.isEmpty()) {
        QFile::remove(m_curlBodyPath);
        m_curlBodyPath.clear();
    }

    QTemporaryFile bodyFile;
    bodyFile.setAutoRemove(false);
    if (!bodyFile.open()) {
        failWith(QStringLiteral("Could not write temp request body for curl."));
        return;
    }
    bodyFile.write(bodyJson);
    bodyFile.close();
    m_curlBodyPath = bodyFile.fileName();

    QStringList args;
    args << QStringLiteral("-sS")
         << QStringLiteral("-X") << QStringLiteral("POST")
         << QStringLiteral("-H") << QStringLiteral("Content-Type: application/json")
         << QStringLiteral("--data-binary") << QStringLiteral("@") + m_curlBodyPath;
    if (!m_apiKey.isEmpty()) {
        args << QStringLiteral("-H")
             << QStringLiteral("Authorization: Bearer %1").arg(m_apiKey);
    }
    args << chatCompletionsUrl();

    m_curl = new QProcess(this);
    connect(m_curl,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus) {
                QProcess *proc = m_curl;
                m_curl = nullptr;
                const QByteArray raw = proc ? proc->readAllStandardOutput() : QByteArray();
                const QByteArray errOut = proc ? proc->readAllStandardError() : QByteArray();
                if (proc)
                    proc->deleteLater();
                if (!m_curlBodyPath.isEmpty()) {
                    QFile::remove(m_curlBodyPath);
                    m_curlBodyPath.clear();
                }

                if (exitCode != 0) {
                    QString detail = QString::fromUtf8(errOut).trimmed();
                    if (detail.isEmpty())
                        detail = QString::fromUtf8(raw).trimmed();
                    failWith(QStringLiteral("LLM curl error (exit %1):\n%2")
                                 .arg(exitCode)
                                 .arg(detail));
                    return;
                }

                handleHttpPayload(raw, {});
            });
    m_curl->start(curl, args);
    if (!m_curl->waitForStarted(5000)) {
        failWith(QStringLiteral("Failed to start curl for HTTPS LLM request."));
        m_curl->deleteLater();
        m_curl = nullptr;
        QFile::remove(m_curlBodyPath);
        m_curlBodyPath.clear();
    }
}

/*!*******************************************************************************************************************
 * \brief Handles a finished Qt network reply (retries via curl on TLS errors).
 **********************************************************************************************************************/
void AssistantAgent::onNetworkFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply) {
        failWith(QStringLiteral("Empty network reply."));
        return;
    }
    reply->deleteLater();

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        m_busy = false;
        return;
    }

    const QByteArray raw = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        const QString err = reply->errorString();
        // Auto-retry once via curl when Qt TLS/OpenSSL fails.
        if (err.contains(QStringLiteral("TLS"), Qt::CaseInsensitive)
            || err.contains(QStringLiteral("SSL"), Qt::CaseInsensitive)) {
            QJsonObject body;
            body.insert(QStringLiteral("model"), m_model);
            body.insert(QStringLiteral("messages"), m_messages);
            const QJsonArray tools = openaiToolsManifest();
            if (!tools.isEmpty()) {
                body.insert(QStringLiteral("tools"), tools);
                body.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
            }
            emit statusChanged(QStringLiteral("Retry via curl…"));
            postViaCurl(QJsonDocument(body).toJson(QJsonDocument::Compact));
            return;
        }
        handleHttpPayload(raw, err);
        return;
    }

    handleHttpPayload(raw, {});
}

/*!*******************************************************************************************************************
 * \brief Parses a Chat Completions JSON payload or reports a transport error.
 **********************************************************************************************************************/
void AssistantAgent::handleHttpPayload(const QByteArray &raw, const QString &transportError)
{
    if (!transportError.isEmpty()) {
        QString detail = QString::fromUtf8(raw).trimmed();
        if (detail.size() > 600)
            detail = detail.left(600) + QStringLiteral("…");
        failWith(QStringLiteral("LLM HTTP error: %1\n%2").arg(transportError, detail));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject()) {
        failWith(QStringLiteral("LLM returned non-JSON response:\n%1")
                     .arg(QString::fromUtf8(raw.left(400))));
        return;
    }

    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("error"))) {
        const QJsonValue err = root.value(QStringLiteral("error"));
        const QString msg = err.isObject()
                                ? err.toObject().value(QStringLiteral("message")).toString()
                                : err.toString();
        failWith(QStringLiteral("LLM error: %1").arg(msg.isEmpty() ? QString::fromUtf8(raw) : msg));
        return;
    }

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        failWith(QStringLiteral("LLM returned no choices."));
        return;
    }

    const QJsonObject message = choices.at(0).toObject().value(QStringLiteral("message")).toObject();
    handleChoiceMessage(message);
}

/*!*******************************************************************************************************************
 * \brief Processes an assistant message: runs tool_calls or finishes with text.
 **********************************************************************************************************************/
void AssistantAgent::handleChoiceMessage(const QJsonObject &message)
{
    m_messages.append(message);

    const QJsonArray toolCalls = message.value(QStringLiteral("tool_calls")).toArray();
    if (!toolCalls.isEmpty()) {
        emit statusChanged(QStringLiteral("Running tools…"));
        for (const QJsonValue &tv : toolCalls) {
            const QJsonObject call = tv.toObject();
            const QString id = call.value(QStringLiteral("id")).toString();
            const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
            const QString name = fn.value(QStringLiteral("name")).toString();
            const QString argsRaw = fn.value(QStringLiteral("arguments")).toString();

            QJsonObject args;
            const QJsonDocument argsDoc = QJsonDocument::fromJson(argsRaw.toUtf8());
            if (argsDoc.isObject())
                args = argsDoc.object();

            emit statusChanged(QStringLiteral("Tool: %1").arg(name));
            const QJsonObject result = m_mcp ? m_mcp->callTool(name, args)
                                             : QJsonObject{{QStringLiteral("ok"), false},
                                                           {QStringLiteral("error"),
                                                            QStringLiteral("No MCP")}};

            QJsonObject toolMsg;
            toolMsg.insert(QStringLiteral("role"), QStringLiteral("tool"));
            toolMsg.insert(QStringLiteral("tool_call_id"), id);
            toolMsg.insert(QStringLiteral("name"), name);
            toolMsg.insert(QStringLiteral("content"),
                           QString::fromUtf8(
                               QJsonDocument(result).toJson(QJsonDocument::Compact)));
            m_messages.append(toolMsg);
        }
        emit statusChanged(QStringLiteral("Calling model…"));
        postNextCompletion();
        return;
    }

    QString content = message.value(QStringLiteral("content")).toString().trimmed();
    if (content.isEmpty())
        content = QStringLiteral("(empty model reply)");
    finishWithText(content);
}

/*!*******************************************************************************************************************
 * \brief Completes the turn successfully and emits \c finished.
 **********************************************************************************************************************/
void AssistantAgent::finishWithText(const QString &text)
{
    m_busy = false;
    m_round = 0;
    emit statusChanged(QStringLiteral("Ready"));
    emit finished(text);
}

/*!*******************************************************************************************************************
 * \brief Completes the turn with an error and emits \c failed.
 **********************************************************************************************************************/
void AssistantAgent::failWith(const QString &error)
{
    m_busy = false;
    m_round = 0;
    emit statusChanged(QStringLiteral("Ready"));
    emit failed(error);
}
