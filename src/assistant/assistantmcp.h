#ifndef ASSISTANTMCP_H
#define ASSISTANTMCP_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

/*! In-process MCP-style tool registry for the DOOR Assistant (same pattern as EMStudio). */
class AssistantMcp : public QObject
{
    Q_OBJECT

public:
    using ToolHandler = std::function<QJsonObject(const QJsonObject &args)>;

    struct Tool {
        QString name;
        QString description;
        QJsonObject inputSchema;
        ToolHandler handler;
    };

    explicit AssistantMcp(QObject *parent = nullptr);

    void registerTool(const QString &name,
                      const QString &description,
                      const QJsonObject &inputSchema,
                      ToolHandler handler);

    QVector<Tool> tools() const { return m_tools; }
    QJsonArray toolsManifest() const;
    bool hasTool(const QString &name) const;
    QJsonObject callTool(const QString &name, const QJsonObject &args = QJsonObject()) const;

    /*! Keyword / help fallback when the LLM agent is not configured. */
    QString handleUserMessage(const QString &userText) const;

    static QJsonObject okResult(const QJsonObject &data = QJsonObject());
    static QJsonObject errResult(const QString &message);

private:
    static QString formatValue(const QJsonValue &v, int indent = 0);
    QString formatToolReply(const QString &toolName, const QJsonObject &result) const;
    QString formatListToolsReply() const;

    QVector<Tool> m_tools;
};

#endif // ASSISTANTMCP_H
