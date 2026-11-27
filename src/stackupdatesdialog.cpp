#include "stackupdatesdialog.h"
#include "ui_stackupdatesdialog.h"

#include "property.h"
#include "view_tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>

StackUpdatesDialog::StackUpdatesDialog(Properties *properties, QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::StackUpdatesDialog)
    , m_properties(properties)
{
    m_ui->setupUi(this);
    setWindowTitle(tr("Check for updates"));

    connect(m_ui->btnRefresh, &QPushButton::clicked, this, &StackUpdatesDialog::onRefreshClicked);
    connect(m_ui->btnUpdateAll, &QPushButton::clicked, this, &StackUpdatesDialog::onUpdateAllClicked);
    connect(&m_probe, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &StackUpdatesDialog::onProbeFinished);

    buildRows();
    QTimer::singleShot(0, this, &StackUpdatesDialog::startProbes);
}

StackUpdatesDialog::~StackUpdatesDialog()
{
    clearProbe();
    delete m_ui;
}

void StackUpdatesDialog::buildRows()
{
    QGridLayout *grid = m_ui->toolsGrid;
    while (QLayoutItem *item = grid->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    m_rows.clear();

    const QStringList ids = {QStringLiteral("door"), QStringLiteral("qucs"), QStringLiteral("xschem"),
                             QStringLiteral("klayout"), QStringLiteral("emstudio"),
                             QStringLiteral("room")};
    const QStringList names = {QStringLiteral("Door"), QStringLiteral("Qucs-S"),
                               QStringLiteral("Xschem"), QStringLiteral("KLayout"),
                               QStringLiteral("EMStudio"), QStringLiteral("Room")};

    int row = 0;
    auto *hdrName = new QLabel(tr("Tool"), this);
    auto *hdrPath = new QLabel(tr("Path"), this);
    auto *hdrVer = new QLabel(tr("Installed"), this);
    auto *hdrLatest = new QLabel(tr("Latest"), this);
    QFont bold = hdrName->font();
    bold.setBold(true);
    for (QLabel *h : {hdrName, hdrPath, hdrVer, hdrLatest}) {
        h->setFont(bold);
    }
    grid->addWidget(hdrName, row, 0);
    grid->addWidget(hdrPath, row, 1);
    grid->addWidget(hdrVer, row, 2);
    grid->addWidget(hdrLatest, row, 3);
    ++row;

    for (int i = 0; i < ids.size(); ++i) {
        ToolRow entry;
        entry.id = ids.at(i);
        entry.displayName = names.at(i);
        entry.path = resolveToolPath(entry.id);

        auto *nameLbl = new QLabel(entry.displayName, this);
        entry.pathLabel = new QLabel(this);
        entry.pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        entry.pathLabel->setWordWrap(true);
        if (entry.path.isEmpty()) {
            entry.pathLabel->setText(
#ifdef Q_OS_WIN
                entry.id == QStringLiteral("xschem")
                    ? tr("(not set — Windows: use WSL / not bundled yet)")
                    : tr("(not set)")
#else
                tr("(not set)")
#endif
            );
            entry.pathLabel->setStyleSheet(QStringLiteral("color: gray;"));
        } else {
            entry.pathLabel->setText(QDir::toNativeSeparators(entry.path));
            entry.pathLabel->setToolTip(entry.pathLabel->text());
        }

        entry.versionLabel = new QLabel(entry.path.isEmpty() ? tr("—") : tr("loading…"), this);
        entry.latestLabel = new QLabel(tr("—"), this);
        entry.latestLabel->setStyleSheet(QStringLiteral("color: gray;"));

        entry.updateButton = new QPushButton(tr("Update"), this);
        entry.updateButton->setProperty("toolIndex", i);
        entry.updateButton->setToolTip(tr("Download/install from CI is not wired yet."));
        connect(entry.updateButton, &QPushButton::clicked, this, &StackUpdatesDialog::onRowUpdateClicked);

        grid->addWidget(nameLbl, row, 0);
        grid->addWidget(entry.pathLabel, row, 1);
        grid->addWidget(entry.versionLabel, row, 2);
        grid->addWidget(entry.latestLabel, row, 3);
        grid->addWidget(entry.updateButton, row, 4);
        m_rows.append(entry);
        ++row;
    }

    grid->setColumnStretch(1, 1);
}

QString StackUpdatesDialog::resolveToolPath(const QString &toolId) const
{
    if (!m_properties) {
        return {};
    }

    if (toolId == QStringLiteral("door")) {
        return QCoreApplication::applicationFilePath();
    }
    if (toolId == QStringLiteral("room")) {
        if (m_properties->exists(QStringLiteral("RoomPath"))) {
            return m_properties->get<QString>(QStringLiteral("RoomPath")).trimmed();
        }
        return {};
    }
    if (toolId == QStringLiteral("qucs") || toolId == QStringLiteral("xschem")) {
        const QVector<ViewToolEntry> tools = loadViewTools(m_properties, QStringLiteral("Schematic"));
        for (const ViewToolEntry &e : tools) {
            const QString n = e.name.toLower();
            const QString p = resolveToolLaunchPath(e.path);
            if (toolId == QStringLiteral("qucs")
                && (n.contains(QStringLiteral("qucs")) || p.toLower().contains(QStringLiteral("qucs")))) {
                return p;
            }
            if (toolId == QStringLiteral("xschem")
                && (n.contains(QStringLiteral("xschem"))
                    || p.toLower().contains(QStringLiteral("xschem")))) {
                return p;
            }
        }
        // Fallback: legacy Schematic path if it matches the tool name.
        if (m_properties->exists(QStringLiteral("Schematic"))) {
            const QString p =
                resolveToolLaunchPath(m_properties->get<QString>(QStringLiteral("Schematic")));
            const QString pl = p.toLower();
            if (toolId == QStringLiteral("qucs") && pl.contains(QStringLiteral("qucs"))) {
                return p;
            }
            if (toolId == QStringLiteral("xschem") && pl.contains(QStringLiteral("xschem"))) {
                return p;
            }
        }
        return {};
    }
    if (toolId == QStringLiteral("klayout")) {
        const QString fromViews =
            resolveToolLaunchPath(resolveViewToolPath(loadViewTools(m_properties, QStringLiteral("Layout")),
                                                      const_cast<StackUpdatesDialog *>(this)));
        if (!fromViews.isEmpty()) {
            return fromViews;
        }
        if (m_properties->exists(QStringLiteral("Layout"))) {
            return resolveToolLaunchPath(m_properties->get<QString>(QStringLiteral("Layout")));
        }
        return {};
    }
    if (toolId == QStringLiteral("emstudio")) {
        const QString fromViews =
            resolveToolLaunchPath(resolveViewToolPath(loadViewTools(m_properties, QStringLiteral("EM")),
                                                      const_cast<StackUpdatesDialog *>(this)));
        if (!fromViews.isEmpty()) {
            return fromViews;
        }
        if (m_properties->exists(QStringLiteral("EMSTUDIO"))) {
            return resolveToolLaunchPath(m_properties->get<QString>(QStringLiteral("EMSTUDIO")));
        }
        return {};
    }
    return {};
}

QString StackUpdatesDialog::probeProgram(const QString &path) const
{
    if (path.isEmpty()) {
        return {};
    }
    // Layout tool may be: "klayout.exe" -e  or quoted path with args.
    QString trimmed = path.trimmed();
    if (trimmed.startsWith(QLatin1Char('"'))) {
        const int end = trimmed.indexOf(QLatin1Char('"'), 1);
        if (end > 1) {
            return trimmed.mid(1, end - 1);
        }
    }
    const QStringList parts = QProcess::splitCommand(trimmed);
    return parts.isEmpty() ? trimmed : parts.first();
}

void StackUpdatesDialog::clearProbe()
{
    if (m_probe.state() != QProcess::NotRunning) {
        m_probe.kill();
        m_probe.waitForFinished(500);
    }
    m_probeIndex = -1;
}

void StackUpdatesDialog::startProbes()
{
    clearProbe();
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].path.isEmpty()) {
            setVersionText(i, tr("not found"));
            m_rows[i].probePending = false;
        } else if (m_rows[i].id == QStringLiteral("room")) {
            const QFileInfo fi(m_rows[i].path);
            setVersionText(i, fi.exists() ? tr("present") : tr("missing"));
            m_rows[i].latestLabel->setText(tr("—"));
            m_rows[i].probePending = false;
        } else {
            m_rows[i].probePending = true;
            setVersionText(i, tr("loading…"));
        }
    }
    runNextProbe();
}

void StackUpdatesDialog::runNextProbe()
{
    m_probeIndex = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].probePending) {
            m_probeIndex = i;
            break;
        }
    }
    if (m_probeIndex < 0) {
        return;
    }

    const QString prog = probeProgram(m_rows[m_probeIndex].path);
    if (prog.isEmpty() || !QFileInfo::exists(prog)) {
        setVersionText(m_probeIndex, tr("not found"));
        m_rows[m_probeIndex].probePending = false;
        runNextProbe();
        return;
    }

    m_probe.setProgram(prog);
    m_probe.setArguments({QStringLiteral("--version")});
    m_probe.setWorkingDirectory(QFileInfo(prog).absolutePath());
    m_probe.start();
    if (!m_probe.waitForStarted(2000)) {
        // Some tools only accept -v
        m_probe.setArguments({QStringLiteral("-v")});
        m_probe.start();
        if (!m_probe.waitForStarted(2000)) {
            setVersionText(m_probeIndex, tr("unreachable"));
            m_rows[m_probeIndex].probePending = false;
            runNextProbe();
        }
    }
}

void StackUpdatesDialog::onProbeFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(exitCode);
    if (m_probeIndex < 0 || m_probeIndex >= m_rows.size()) {
        return;
    }
    const int idx = m_probeIndex;
    m_rows[idx].probePending = false;

    const QByteArray out = m_probe.readAllStandardOutput() + m_probe.readAllStandardError();
    if (status != QProcess::NormalExit && out.isEmpty()) {
        // Retry once with -v if --version failed silently
        if (m_probe.arguments().value(0) == QStringLiteral("--version")) {
            m_rows[idx].probePending = true;
            m_probe.setArguments({QStringLiteral("-v")});
            m_probe.start();
            if (m_probe.waitForStarted(2000)) {
                return;
            }
            m_rows[idx].probePending = false;
        }
        setVersionText(idx, tr("unknown"));
    } else {
        const QString ver = extractVersion(out);
        setVersionText(idx, ver.isEmpty() ? tr("installed") : ver);
    }
    m_rows[idx].latestLabel->setText(tr("—"));
    runNextProbe();
}

QString StackUpdatesDialog::extractVersion(const QByteArray &raw)
{
    const QString text = QString::fromLocal8Bit(raw);
    static const QRegularExpression re(
        QStringLiteral(R"((?i)\bv?(\d+\.\d+(?:\.\d+)?(?:[-._][A-Za-z0-9]+)?)\b)"));
    const QRegularExpressionMatch m = re.match(text);
    return m.hasMatch() ? m.captured(1) : QString();
}

void StackUpdatesDialog::setVersionText(int index, const QString &text)
{
    if (index < 0 || index >= m_rows.size() || !m_rows[index].versionLabel) {
        return;
    }
    m_rows[index].versionLabel->setText(text);
}

void StackUpdatesDialog::onRefreshClicked()
{
    for (int i = 0; i < m_rows.size(); ++i) {
        m_rows[i].path = resolveToolPath(m_rows[i].id);
        if (m_rows[i].pathLabel) {
            if (m_rows[i].path.isEmpty()) {
                m_rows[i].pathLabel->setText(tr("(not set)"));
            } else {
                m_rows[i].pathLabel->setText(QDir::toNativeSeparators(m_rows[i].path));
            }
        }
    }
    startProbes();
}

void StackUpdatesDialog::onUpdateAllClicked()
{
    QMessageBox::information(
        this, tr("Check for updates"),
        tr("CI download/install is not implemented yet.\n"
           "On Linux use install_room_stack_from_ci.sh --update.\n"
           "Windows portable artifacts are being added as *-windows zips."));
}

void StackUpdatesDialog::onRowUpdateClicked()
{
    auto *btn = qobject_cast<QPushButton *>(sender());
    if (!btn) {
        return;
    }
    const int idx = btn->property("toolIndex").toInt();
    QString name = (idx >= 0 && idx < m_rows.size()) ? m_rows[idx].displayName : tr("tool");
#ifdef Q_OS_WIN
    if (idx >= 0 && idx < m_rows.size() && m_rows[idx].id == QStringLiteral("xschem")) {
        QMessageBox::information(
            this, tr("Update %1").arg(name),
            tr("Xschem has no native Windows portable bundle yet. Use WSL or a Linux host."));
        return;
    }
#endif
    QMessageBox::information(
        this, tr("Update %1").arg(name),
        tr("Update for %1 is not wired yet (CI artifact download coming next).").arg(name));
}
