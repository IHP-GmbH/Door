#ifndef ROOMMCPCLIENT_H
#define ROOMMCPCLIENT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QProcess;

/*! Stdio MCP client for Room's ``python -m room mcp`` server. */
class RoomMcpClient : public QObject
{
    Q_OBJECT

public:
    explicit RoomMcpClient(QObject *parent = nullptr);
    ~RoomMcpClient() override;

    bool ensureStarted();
    void stop();
    bool isReady() const { return m_ready; }

    QJsonArray listTools();
    QJsonObject callTool(const QString &name, const QJsonObject &args = QJsonObject());

private:
    QString findPython() const;
    QString findRoomPackageDir() const;
    QString findRoomCLibrary() const;
    QJsonObject request(const QString &method, const QJsonObject &params = QJsonObject());

    QProcess *m_proc = nullptr;
    bool m_ready = false;
    int m_nextId = 1;
};

#endif // ROOMMCPCLIENT_H
