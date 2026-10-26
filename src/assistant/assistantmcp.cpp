#include "assistantmcp.h"

#include <QJsonDocument>

AssistantMcp::AssistantMcp(QObject *parent)
    : QObject(parent)
{
}

void AssistantMcp::registerTool(const QString &name,
                                const QString &description,
                                const QJsonObject &inputSchema,
                                ToolHandler handler)
{
    for (Tool &t : m_tools) {
        if (t.name == name) {
            t.description = description;
            t.inputSchema = inputSchema;
            t.handler = std::move(handler);
            return;
        }
    }
    Tool tool;
    tool.name = name;
    tool.description = description;
    tool.inputSchema = inputSchema;
    tool.handler = std::move(handler);
    m_tools.push_back(std::move(tool));
}

QJsonArray AssistantMcp::toolsManifest() const
{
    QJsonArray out;
    for (const Tool &t : m_tools) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), t.name);
        o.insert(QStringLiteral("description"), t.description);
        o.insert(QStringLiteral("inputSchema"), t.inputSchema);
        out.append(o);
    }
    return out;
}

bool AssistantMcp::hasTool(const QString &name) const
{
    for (const Tool &t : m_tools) {
        if (t.name == name) {
            return true;
        }
    }
    return false;
}

QJsonObject AssistantMcp::callTool(const QString &name, const QJsonObject &args) const
{
    for (const Tool &t : m_tools) {
        if (t.name != name) {
            continue;
        }
        if (!t.handler) {
            return errResult(QStringLiteral("Tool has no handler: %1").arg(name));
        }
        try {
            return t.handler(args);
        } catch (const std::exception &ex) {
            return errResult(QString::fromUtf8(ex.what()));
        } catch (...) {
            return errResult(QStringLiteral("Tool failed: %1").arg(name));
        }
    }
    return errResult(QStringLiteral("Unknown tool: %1").arg(name));
}

QJsonObject AssistantMcp::okResult(const QJsonObject &data)
{
    QJsonObject o = data;
    o.insert(QStringLiteral("ok"), true);
    return o;
}

QJsonObject AssistantMcp::errResult(const QString &message)
{
    QJsonObject o;
    o.insert(QStringLiteral("ok"), false);
    o.insert(QStringLiteral("error"), message);
    return o;
}

QString AssistantMcp::formatValue(const QJsonValue &v, int indent)
{
    const QString pad = QString(indent, QLatin1Char(' '));
    if (v.isBool()) {
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (v.isDouble()) {
        return QString::number(v.toDouble());
    }
    if (v.isString()) {
        return v.toString();
    }
    if (v.isArray()) {
        QStringList lines;
        const QJsonArray a = v.toArray();
        for (const QJsonValue &item : a) {
            lines << pad + QStringLiteral("- ") + formatValue(item, indent + 2).trimmed();
        }
        return lines.isEmpty() ? QStringLiteral("[]") : lines.join(QLatin1Char('\n'));
    }
    if (v.isObject()) {
        QStringList lines;
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (it.key() == QLatin1String("ok")) {
                continue;
            }
            lines << pad + it.key() + QStringLiteral(": ") + formatValue(it.value(), indent + 2).trimmed();
        }
        return lines.join(QLatin1Char('\n'));
    }
    return QStringLiteral("null");
}

QString AssistantMcp::formatToolReply(const QString &toolName, const QJsonObject &result) const
{
    if (!result.value(QStringLiteral("ok")).toBool(true)
        && result.contains(QStringLiteral("error"))) {
        return QStringLiteral("Tool `%1` failed: %2")
            .arg(toolName, result.value(QStringLiteral("error")).toString());
    }
    const QString body = formatValue(result).trimmed();
    if (body.isEmpty()) {
        return QStringLiteral("Tool `%1` completed.").arg(toolName);
    }
    return QStringLiteral("Tool `%1`:\n%2").arg(toolName, body);
}

QString AssistantMcp::formatListToolsReply() const
{
    QStringList lines;
    lines << QStringLiteral("Available tools:");
    for (const Tool &t : m_tools) {
        lines << QStringLiteral("- **%1** — %2").arg(t.name, t.description);
    }
    lines << QStringLiteral(
        "\nConfigure Preferences → Assistant (API key / base URL / model) for full LLM mode.");
    return lines.join(QLatin1Char('\n'));
}

QString AssistantMcp::handleUserMessage(const QString &userText) const
{
    const QString text = userText.trimmed();
    if (text.isEmpty()) {
        return QStringLiteral("Send a question, or ask for `list_tools`.");
    }
    const QString lower = text.toLower();
    if (lower.contains(QStringLiteral("list_tools")) || lower == QStringLiteral("help")
        || lower == QStringLiteral("?")) {
        if (hasTool(QStringLiteral("list_tools"))) {
            return formatToolReply(QStringLiteral("list_tools"),
                                   callTool(QStringLiteral("list_tools")));
        }
        return formatListToolsReply();
    }
    if (lower.contains(QStringLiteral("app_state")) || lower.contains(QStringLiteral("selection"))
        || lower.contains(QStringLiteral("status"))) {
        if (hasTool(QStringLiteral("get_app_state"))) {
            return formatToolReply(QStringLiteral("get_app_state"),
                                   callTool(QStringLiteral("get_app_state")));
        }
    }
    if (hasTool(QStringLiteral("list_tools"))) {
        return formatListToolsReply()
            + QStringLiteral(
                  "\n\n(No LLM configured — enable Preferences → Assistant for natural language.)");
    }
    return QStringLiteral(
        "Assistant heuristic mode: ask `list_tools` or configure Preferences → Assistant.");
}
