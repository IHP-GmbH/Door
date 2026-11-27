#include "stackupdatesdialog.h"
#include "ui_stackupdatesdialog.h"

#include "property.h"
#include "view_tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

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
    connect(&m_ghToken, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &StackUpdatesDialog::onGhTokenFinished);

    buildRows();
    ensureGithubToken();
    QTimer::singleShot(0, this, &StackUpdatesDialog::startProbes);
}

StackUpdatesDialog::~StackUpdatesDialog()
{
    clearProbe();
    if (m_latestReply) {
        m_latestReply->abort();
        m_latestReply->deleteLater();
        m_latestReply = nullptr;
    }
    delete m_ui;
}

QString StackUpdatesDialog::repoForTool(const QString &toolId)
{
    if (toolId == QLatin1String("door")) {
        return QStringLiteral("IHP-GmbH/Door");
    }
    if (toolId == QLatin1String("qucs")) {
        return QStringLiteral("adatsuk/Qucs-S-roomdb");
    }
    if (toolId == QLatin1String("xschem")) {
        return QStringLiteral("adatsuk/XSchem-roomdb");
    }
    if (toolId == QLatin1String("klayout")) {
        return QStringLiteral("adatsuk/KLayout-roomdb");
    }
    if (toolId == QLatin1String("emstudio")) {
        return QStringLiteral("IHP-GmbH/EMStudio");
    }
    if (toolId == QLatin1String("room")) {
        return QStringLiteral("IHP-GmbH/Room");
    }
    return {};
}

QString StackUpdatesDialog::artifactForTool(const QString &toolId)
{
#ifdef Q_OS_WIN
    if (toolId == QLatin1String("door")) {
        return QStringLiteral("door-windows");
    }
    if (toolId == QLatin1String("qucs")) {
        return QStringLiteral("qucs-s-windows");
    }
    if (toolId == QLatin1String("klayout")) {
        return QStringLiteral("klayout-windows");
    }
    if (toolId == QLatin1String("emstudio")) {
        return QStringLiteral("emstudio-windows");
    }
    if (toolId == QLatin1String("xschem")) {
        return {}; // no native Windows portable yet
    }
#else
    // Prefer Rocky 8 portable names (glibc 2.28); Door updater can refine later.
    if (toolId == QLatin1String("door")) {
        return QStringLiteral("door-rhel8");
    }
    if (toolId == QLatin1String("qucs")) {
        return QStringLiteral("qucs-s-rocky8");
    }
    if (toolId == QLatin1String("xschem")) {
        return QStringLiteral("xschem-rocky8");
    }
    if (toolId == QLatin1String("klayout")) {
        return QStringLiteral("klayout-rocky8");
    }
    if (toolId == QLatin1String("emstudio")) {
        return QStringLiteral("emstudio-linux-rocky8");
    }
#endif
    return {};
}

QString StackUpdatesDialog::shortDate(const QString &isoUtc)
{
    // 2026-11-26T13:11:10Z → 2026-11-26
    if (isoUtc.size() >= 10) {
        return isoUtc.left(10);
    }
    return isoUtc;
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
        entry.repo = repoForTool(entry.id);
        entry.artifactName = artifactForTool(entry.id);
        entry.installedArtifactId = readInstalledArtifactId(entry.id, entry.path);

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
        entry.latestLabel = new QLabel(tr("…"), this);
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

QString StackUpdatesDialog::readInstalledArtifactId(const QString &toolId, const QString &path) const
{
    Q_UNUSED(toolId);
    if (path.isEmpty()) {
        return {};
    }

    QStringList candidates;
    const QFileInfo fi(path);
    if (fi.isDir()) {
        candidates << QDir(path).filePath(QStringLiteral("BUNDLE_SOURCE.txt"));
    } else {
        const QDir dir = fi.absoluteDir();
        candidates << dir.filePath(QStringLiteral("BUNDLE_SOURCE.txt"));
        candidates << dir.filePath(QStringLiteral("../BUNDLE_SOURCE.txt"));
        // Linux room-stack layout: …/lib/room-stack/<tool>/…/BUNDLE_SOURCE.txt
        QDir climb = dir;
        for (int i = 0; i < 5; ++i) {
            candidates << climb.filePath(QStringLiteral("BUNDLE_SOURCE.txt"));
            if (!climb.cdUp()) {
                break;
            }
        }
    }

    for (const QString &c : candidates) {
        QFile f(c);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        while (!f.atEnd()) {
            const QByteArray line = f.readLine().trimmed();
            if (line.startsWith("artifact_id=")) {
                return QString::fromUtf8(line.mid(12)).trimmed();
            }
        }
    }
    return {};
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

void StackUpdatesDialog::ensureGithubToken()
{
    m_githubToken = qEnvironmentVariable("GH_TOKEN").trimmed();
    if (m_githubToken.isEmpty()) {
        m_githubToken = qEnvironmentVariable("GITHUB_TOKEN").trimmed();
    }
    if (!m_githubToken.isEmpty()) {
        m_tokenReady = true;
        return;
    }

    const QString gh = QStandardPaths::findExecutable(QStringLiteral("gh"));
    if (gh.isEmpty()) {
        m_tokenReady = true; // proceed unauthenticated (may 401 on private repos)
        return;
    }
    m_ghToken.setProgram(gh);
    m_ghToken.setArguments({QStringLiteral("auth"), QStringLiteral("token")});
    m_ghToken.start();
}

void StackUpdatesDialog::onGhTokenFinished(int exitCode, QProcess::ExitStatus status)
{
    if (status == QProcess::NormalExit && exitCode == 0) {
        m_githubToken = QString::fromUtf8(m_ghToken.readAllStandardOutput()).trimmed();
    }
    m_tokenReady = true;
    // If probes already finished, kick latest checks now.
    bool anyProbe = false;
    for (const ToolRow &r : m_rows) {
        if (r.probePending) {
            anyProbe = true;
            break;
        }
    }
    if (!anyProbe) {
        startLatestChecks();
    }
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
            m_rows[i].probePending = false;
        } else {
            m_rows[i].probePending = true;
            setVersionText(i, tr("loading…"));
        }
        setLatestText(i, tr("…"));
        m_rows[i].latestPending = true;
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
        if (m_tokenReady) {
            startLatestChecks();
        }
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
    runNextProbe();
}

void StackUpdatesDialog::startLatestChecks()
{
    m_latestIndex = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        m_rows[i].latestPending = true;
        setLatestText(i, tr("checking…"));
    }
    runNextLatest();
}

void StackUpdatesDialog::runNextLatest()
{
    m_latestIndex = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].latestPending) {
            m_latestIndex = i;
            break;
        }
    }
    if (m_latestIndex < 0) {
        m_ui->lblStatus->setText(
            tr("Latest = newest non-expired GitHub Actions artifact (or Room HEAD). "
               "Update download is not wired yet."));
        return;
    }

    ToolRow &row = m_rows[m_latestIndex];
#ifdef Q_OS_WIN
    if (row.id == QStringLiteral("xschem")) {
        applyLatestResult(m_latestIndex, {}, {}, tr("no Windows artifact (use WSL)"));
        return;
    }
#endif
    if (row.id == QStringLiteral("room")) {
        requestRoomLatest(m_latestIndex);
        return;
    }
    if (row.artifactName.isEmpty() || row.repo.isEmpty()) {
        applyLatestResult(m_latestIndex, {}, {}, tr("no CI artifact mapping"));
        return;
    }
    requestArtifactLatest(m_latestIndex);
}

void StackUpdatesDialog::requestArtifactLatest(int index)
{
    const ToolRow &row = m_rows.at(index);
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/actions/artifacts?name=%2&per_page=20")
                       .arg(row.repo, row.artifactName));
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "Door-StackUpdates/1.0");
    if (!m_githubToken.isEmpty()) {
        req.setRawHeader("Authorization", QByteArray("Bearer ") + m_githubToken.toUtf8());
    }
    if (m_latestReply) {
        m_latestReply->abort();
        m_latestReply->deleteLater();
    }
    m_latestReply = m_nam.get(req);
    connect(m_latestReply, &QNetworkReply::finished, this, &StackUpdatesDialog::onLatestFinished);
}

void StackUpdatesDialog::requestRoomLatest(int index)
{
    Q_UNUSED(index);
    const QUrl url(QStringLiteral("https://api.github.com/repos/IHP-GmbH/Room/commits?per_page=1"));
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "Door-StackUpdates/1.0");
    if (!m_githubToken.isEmpty()) {
        req.setRawHeader("Authorization", QByteArray("Bearer ") + m_githubToken.toUtf8());
    }
    if (m_latestReply) {
        m_latestReply->abort();
        m_latestReply->deleteLater();
    }
    m_latestReply = m_nam.get(req);
    connect(m_latestReply, &QNetworkReply::finished, this, &StackUpdatesDialog::onLatestFinished);
}

void StackUpdatesDialog::onLatestFinished()
{
    QNetworkReply *reply = m_latestReply;
    m_latestReply = nullptr;
    if (!reply) {
        return;
    }
    reply->deleteLater();

    const int idx = m_latestIndex;
    if (idx < 0 || idx >= m_rows.size()) {
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError || status >= 400) {
        QString err = reply->errorString();
        if (status == 401 || status == 403) {
            err = tr("GitHub auth required (set GH_TOKEN or run: gh auth login)");
        } else if (status > 0) {
            err = tr("HTTP %1").arg(status);
        }
        applyLatestResult(idx, {}, {}, err);
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (m_rows[idx].id == QStringLiteral("room")) {
        QString sha;
        QString date;
        if (doc.isArray() && !doc.array().isEmpty()) {
            const QJsonObject o = doc.array().at(0).toObject();
            sha = o.value(QStringLiteral("sha")).toString().left(7);
            date = o.value(QStringLiteral("commit"))
                       .toObject()
                       .value(QStringLiteral("committer"))
                       .toObject()
                       .value(QStringLiteral("date"))
                       .toString();
        }
        if (sha.isEmpty()) {
            applyLatestResult(idx, {}, {}, tr("no commits"));
        } else {
            applyLatestResult(idx, sha, date, {});
        }
        return;
    }

    QString artId;
    QString created;
    if (doc.isObject()) {
        const QJsonArray arts = doc.object().value(QStringLiteral("artifacts")).toArray();
        for (const QJsonValue &v : arts) {
            const QJsonObject a = v.toObject();
            if (a.value(QStringLiteral("expired")).toBool()) {
                continue;
            }
            artId = QString::number(a.value(QStringLiteral("id")).toVariant().toLongLong());
            created = a.value(QStringLiteral("created_at")).toString();
            if (!artId.isEmpty()) {
                break;
            }
        }
    }
    if (artId.isEmpty()) {
        applyLatestResult(idx, {}, {}, tr("no artifact yet (%1)").arg(m_rows[idx].artifactName));
        return;
    }
    applyLatestResult(idx, artId, created, {});
}

void StackUpdatesDialog::applyLatestResult(int index, const QString &artifactId,
                                           const QString &createdAt, const QString &error)
{
    if (index < 0 || index >= m_rows.size()) {
        return;
    }
    m_rows[index].latestPending = false;

    if (!error.isEmpty()) {
        setLatestText(index, tr("—"), error);
        runNextLatest();
        return;
    }

    const QString date = shortDate(createdAt);
    QString text;
    QString tip;

    if (m_rows[index].id == QStringLiteral("room")) {
        text = artifactId; // short SHA
        if (!date.isEmpty()) {
            text += QStringLiteral(" (%1)").arg(date);
        }
        tip = tr("Room HEAD on GitHub");
    } else if (!m_rows[index].installedArtifactId.isEmpty()
               && m_rows[index].installedArtifactId == artifactId) {
        text = date.isEmpty() ? tr("up to date") : tr("%1 (up to date)").arg(date);
        tip = tr("artifact_id=%1").arg(artifactId);
    } else if (!m_rows[index].installedArtifactId.isEmpty()) {
        text = date.isEmpty() ? tr("update available") : tr("%1 (newer)").arg(date);
        tip = tr("installed artifact_id=%1 → latest=%2")
                  .arg(m_rows[index].installedArtifactId, artifactId);
    } else {
        text = date.isEmpty() ? artifactId : date;
        tip = tr("CI artifact %1 (id=%2)").arg(m_rows[index].artifactName, artifactId);
    }

    setLatestText(index, text, tip);
    runNextLatest();
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

void StackUpdatesDialog::setLatestText(int index, const QString &text, const QString &tooltip)
{
    if (index < 0 || index >= m_rows.size() || !m_rows[index].latestLabel) {
        return;
    }
    m_rows[index].latestLabel->setText(text);
    m_rows[index].latestLabel->setToolTip(tooltip);
    m_rows[index].latestLabel->setStyleSheet(
        text.contains(QStringLiteral("newer")) || text.contains(QStringLiteral("update available"))
            ? QStringLiteral("color: #a60;")
            : (text.contains(QStringLiteral("up to date")) ? QStringLiteral("color: green;")
                                                           : QStringLiteral("color: gray;")));
}

void StackUpdatesDialog::onRefreshClicked()
{
    if (m_latestReply) {
        m_latestReply->abort();
        m_latestReply->deleteLater();
        m_latestReply = nullptr;
    }
    for (int i = 0; i < m_rows.size(); ++i) {
        m_rows[i].path = resolveToolPath(m_rows[i].id);
        m_rows[i].installedArtifactId = readInstalledArtifactId(m_rows[i].id, m_rows[i].path);
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
           "Latest column already queries GitHub Actions artifacts."));
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
