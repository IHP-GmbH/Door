#ifndef STACKUPDATESDIALOG_H
#define STACKUPDATESDIALOG_H

#include <QDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QVector>

class Properties;
class QLabel;
class QPushButton;

namespace Ui {
class StackUpdatesDialog;
}

/*! Dialog: Help → Check for updates… — ROOM-stack tool status + CI latest. */
class StackUpdatesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StackUpdatesDialog(Properties *properties, QWidget *parent = nullptr);
    ~StackUpdatesDialog() override;

private slots:
    void onRefreshClicked();
    void onUpdateAllClicked();
    void onRowUpdateClicked();
    void onProbeFinished(int exitCode, QProcess::ExitStatus status);
    void onLatestFinished();
    void onGhApiFinished(int exitCode, QProcess::ExitStatus status);
    void onGhTokenFinished(int exitCode, QProcess::ExitStatus status);

private:
    struct ToolRow {
        QString     id; //!< door|qucs|xschem|klayout|emstudio|room
        QString     displayName;
        QString     path;
        QString     repo;         //!< owner/name for GitHub Actions
        QString     artifactName; //!< CI artifact name for this OS
        QString     installedArtifactId;
        QLabel     *pathLabel = nullptr;
        QLabel     *versionLabel = nullptr;
        QLabel     *latestLabel = nullptr;
        QPushButton *updateButton = nullptr;
        bool        probePending = false;
        bool        latestPending = false;
        bool        triedFallbackArtifact = false;
    };

    void        buildRows();
    void        clearProbe();
    void        startProbes();
    void        runNextProbe();
    void        startLatestChecks();
    void        runNextLatest();
    void        requestArtifactLatest(int index);
    void        requestRoomLatest(int index);
    bool        startGhApi(const QString &endpoint);
    void        startNetworkGet(const QUrl &url);
    void        handleLatestPayload(const QByteArray &body, int httpStatus, const QString &netError);
    void        applyLatestResult(int index, const QString &artifactId, const QString &createdAt,
                                  const QString &error, const QString &ciHeadSha = QString());
    void        ensureGithubToken();
    void        setVersionText(int index, const QString &text);
    void        setLatestText(int index, const QString &text, const QString &tooltip = QString());
    void        setUpdateEnabled(int index, bool enabled, const QString &tooltip = QString());
    void        refreshUpdateAllEnabled();
    QString     resolveToolPath(const QString &toolId) const;
    QString     probeProgram(const QString &path) const;
    QString     readInstalledArtifactId(const QString &toolId, const QString &path) const;
    QString     readLocalGitShortSha(const QString &repoPath) const;
    QString     findGitRoot(const QString &path) const;
    bool        isLocalDevBuild(const QString &toolId, const QString &path) const;
    static QString extractVersion(const QByteArray &raw);
    static QString artifactForTool(const QString &toolId);
    static QString artifactFallbackForTool(const QString &toolId);
    static QString repoForTool(const QString &toolId);
    static QString shortDate(const QString &isoUtc);

    Ui::StackUpdatesDialog *m_ui = nullptr;
    Properties             *m_properties = nullptr;
    QVector<ToolRow>        m_rows;
    QProcess                m_probe;
    QProcess                m_ghToken;
    QProcess                m_ghApi;
    QNetworkAccessManager   m_nam;
    QNetworkReply          *m_latestReply = nullptr;
    QString                 m_githubToken;
    QString                 m_ghExe;
    int                     m_probeIndex = -1;
    int                     m_latestIndex = -1;
    bool                    m_tokenReady = false;
    bool                    m_latestIsRoom = false;
};

#endif // STACKUPDATESDIALOG_H
