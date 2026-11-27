#ifndef STACKUPDATESDIALOG_H
#define STACKUPDATESDIALOG_H

#include <QDialog>
#include <QProcess>
#include <QVector>

class Properties;
class QLabel;
class QPushButton;

namespace Ui {
class StackUpdatesDialog;
}

/*! Dialog: Help → Check for updates… — ROOM-stack tool status + Update stub. */
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

private:
    struct ToolRow {
        QString     id;          //!< door|qucs|xschem|klayout|emstudio|room
        QString     displayName;
        QString     path;
        QLabel     *pathLabel = nullptr;
        QLabel     *versionLabel = nullptr;
        QLabel     *latestLabel = nullptr;
        QPushButton *updateButton = nullptr;
        bool        probePending = false;
    };

    void        buildRows();
    void        clearProbe();
    void        startProbes();
    void        runNextProbe();
    void        setVersionText(int index, const QString &text);
    QString     resolveToolPath(const QString &toolId) const;
    QString     probeProgram(const QString &path) const;
    static QString extractVersion(const QByteArray &raw);

    Ui::StackUpdatesDialog *m_ui = nullptr;
    Properties             *m_properties = nullptr;
    QVector<ToolRow>        m_rows;
    QProcess                m_probe;
    int                     m_probeIndex = -1;
};

#endif // STACKUPDATESDIALOG_H
