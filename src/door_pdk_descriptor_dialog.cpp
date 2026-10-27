#include "door_pdk_descriptor_dialog.h"

#include "door_pdk.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

class JsonSyntaxHighlighter : public QSyntaxHighlighter
{
public:
    explicit JsonSyntaxHighlighter(QTextDocument *parent)
        : QSyntaxHighlighter(parent)
    {
        m_key.setForeground(QColor(QStringLiteral("#0550ae")));
        m_key.setFontWeight(QFont::Bold);
        m_string.setForeground(QColor(QStringLiteral("#0a3069")));
        m_number.setForeground(QColor(QStringLiteral("#953800")));
        m_literal.setForeground(QColor(QStringLiteral("#8250df")));
        m_punct.setForeground(QColor(QStringLiteral("#6e7781")));
    }

protected:
    void highlightBlock(const QString &text) override
    {
        enum State { Normal, InString, Escape };
        State state = Normal;
        bool maybeKey = true;
        int start = 0;

        for (int i = 0; i < text.size(); ++i) {
            const QChar ch = text.at(i);
            if (state == InString) {
                if (ch == QLatin1Char('\\')) {
                    state = Escape;
                } else if (ch == QLatin1Char('"')) {
                    QTextCharFormat fmt = m_string;
                    // Key if next non-space is ':'
                    bool isKey = false;
                    for (int j = i + 1; j < text.size(); ++j) {
                        if (text.at(j).isSpace()) {
                            continue;
                        }
                        isKey = (text.at(j) == QLatin1Char(':'));
                        break;
                    }
                    if (isKey) {
                        fmt = m_key;
                    }
                    setFormat(start, i - start + 1, fmt);
                    state = Normal;
                    maybeKey = false;
                }
                continue;
            }
            if (state == Escape) {
                state = InString;
                continue;
            }

            if (ch == QLatin1Char('"')) {
                start = i;
                state = InString;
                continue;
            }
            if (ch == QLatin1Char('{') || ch == QLatin1Char('}') || ch == QLatin1Char('[')
                || ch == QLatin1Char(']') || ch == QLatin1Char(':') || ch == QLatin1Char(',')) {
                setFormat(i, 1, m_punct);
                if (ch == QLatin1Char('{') || ch == QLatin1Char('[') || ch == QLatin1Char(',')) {
                    maybeKey = true;
                }
                Q_UNUSED(maybeKey);
                continue;
            }
            if (ch.isDigit() || (ch == QLatin1Char('-') && i + 1 < text.size() && text.at(i + 1).isDigit())) {
                int j = i + 1;
                while (j < text.size()
                       && (text.at(j).isDigit() || text.at(j) == QLatin1Char('.')
                           || text.at(j) == QLatin1Char('e') || text.at(j) == QLatin1Char('E')
                           || text.at(j) == QLatin1Char('+') || text.at(j) == QLatin1Char('-'))) {
                    ++j;
                }
                setFormat(i, j - i, m_number);
                i = j - 1;
                continue;
            }
            if (text.mid(i, 4) == QLatin1String("true")
                || text.mid(i, 5) == QLatin1String("false")
                || text.mid(i, 4) == QLatin1String("null")) {
                const int len = text.mid(i, 5).startsWith(QLatin1String("false")) ? 5 : 4;
                setFormat(i, len, m_literal);
                i += len - 1;
            }
        }
        if (state == InString || state == Escape) {
            setFormat(start, text.size() - start, m_string);
        }
    }

private:
    QTextCharFormat m_key;
    QTextCharFormat m_string;
    QTextCharFormat m_number;
    QTextCharFormat m_literal;
    QTextCharFormat m_punct;
};

QString cleanJoin(const QString &root, const QString &rel)
{
    if (rel.isEmpty()) {
        return {};
    }
    if (QDir::isAbsolutePath(rel)) {
        return QDir::cleanPath(rel);
    }
    return QDir::cleanPath(root + QLatin1Char('/') + rel);
}

void stylePathItem(QTreeWidgetItem *item, bool exists)
{
    if (!item) {
        return;
    }
    item->setForeground(0, exists ? QColor(QStringLiteral("#1a7f37"))
                                  : QColor(QStringLiteral("#cf222e")));
    item->setToolTip(0,
                     exists ? QObject::tr("Path exists on disk")
                            : QObject::tr("Missing on disk (relative to PDK root)"));
}

} // namespace

DoorPdkDescriptorDialog::DoorPdkDescriptorDialog(const QString &pdkRoot,
                                                 const QString &descriptorPath,
                                                 QWidget *parent)
    : QDialog(parent)
    , m_pdkRoot(QDir::cleanPath(pdkRoot))
    , m_descriptorPath(QDir::cleanPath(descriptorPath))
{
    setWindowTitle(tr("PDK Descriptor"));
    setMinimumSize(920, 560);
    resize(1100, 640);

    auto *mainLayout = new QVBoxLayout(this);

    m_pathLabel = new QLabel(this);
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathLabel->setText(tr("Descriptor: %1").arg(QDir::toNativeSeparators(m_descriptorPath)));
    mainLayout->addWidget(m_pathLabel);

    m_rulesLabel = new QLabel(
        tr("Rules: paths are relative to the folder that contains .door-pdk.json. "
           "Use globs in libraries[].path (e.g. libs.ref/*) — names come from directories. "
           "exceptions: [] skips named folders (add \"sg13g2_io\" only if needed). "
           "Copy this file into any PDK with the same layout."),
        this);
    m_rulesLabel->setWordWrap(true);
    m_rulesLabel->setStyleSheet(QStringLiteral("color: #57606a;"));
    mainLayout->addWidget(m_rulesLabel);

    auto *tmplRow = new QHBoxLayout;
    tmplRow->addWidget(new QLabel(tr("Template:"), this));
    m_templateCombo = new QComboBox(this);
    m_templateCombo->addItem(tr("(current file)"));
    m_templateCombo->addItem(tr("Open-PDK (libs.ref + libs.tech)"));
    tmplRow->addWidget(m_templateCombo, 1);
    mainLayout->addLayout(tmplRow);
    connect(m_templateCombo, QOverload<int>::of(&QComboBox::activated), this,
            &DoorPdkDescriptorDialog::applyTemplate);

    auto *split = new QSplitter(Qt::Horizontal, this);

    m_editor = new QPlainTextEdit(split);
    QFont mono = m_editor->font();
    mono.setFamily(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    m_editor->setFont(mono);
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_highlighter = new JsonSyntaxHighlighter(m_editor->document());

    m_tree = new QTreeWidget(split);
    m_tree->setHeaderLabels({tr("Resolved paths (relative → disk)")});
    m_tree->setUniformRowHeights(true);
    m_tree->setRootIsDecorated(true);

    split->addWidget(m_editor);
    split->addWidget(m_tree);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    mainLayout->addWidget(split, 1);

    m_statusLabel = new QLabel(this);
    mainLayout->addWidget(m_statusLabel);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *saveBtn = buttons->addButton(tr("Save"), QDialogButtonBox::AcceptRole);
    QPushButton *reloadBtn = buttons->addButton(tr("Reload"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(saveBtn, &QPushButton::clicked, this, &DoorPdkDescriptorDialog::saveDescriptor);
    connect(reloadBtn, &QPushButton::clicked, this, &DoorPdkDescriptorDialog::reloadFromDisk);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(300);
    connect(m_debounce, &QTimer::timeout, this, &DoorPdkDescriptorDialog::refreshPathTree);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &DoorPdkDescriptorDialog::onTextChanged);

    loadFile();
}

void DoorPdkDescriptorDialog::loadFile()
{
    m_updatingEditor = true;
    QFile f(m_descriptorPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_editor->setPlainText(QString::fromUtf8(f.readAll()));
        m_statusLabel->setText(tr("Loaded from disk."));
        m_statusLabel->setStyleSheet(QString());
    } else {
        const QString name = QFileInfo(m_pdkRoot).completeBaseName();
        m_editor->setPlainText(openPdkTemplateJson(name));
        m_statusLabel->setText(
            tr("File missing — showing Open-PDK template. Save to create %1.")
                .arg(QDir::toNativeSeparators(m_descriptorPath)));
        m_statusLabel->setStyleSheet(QStringLiteral("color: #9a6700;"));
    }
    m_updatingEditor = false;
    refreshPathTree();
}

void DoorPdkDescriptorDialog::onTextChanged()
{
    if (m_updatingEditor) {
        return;
    }
    m_debounce->start();
}

void DoorPdkDescriptorDialog::refreshPathTree()
{
    QString err;
    if (!parseAndFillTree(m_editor->toPlainText(), &err)) {
        m_statusLabel->setText(err);
        m_statusLabel->setStyleSheet(QStringLiteral("color: #cf222e;"));
        return;
    }
    m_statusLabel->setText(tr("JSON OK — path tree updated."));
    m_statusLabel->setStyleSheet(QStringLiteral("color: #1a7f37;"));
}

bool DoorPdkDescriptorDialog::parseAndFillTree(const QString &text, QString *errorOut)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut) {
            *errorOut = tr("Invalid JSON: %1 (offset %2)")
                            .arg(err.errorString())
                            .arg(err.offset);
        }
        return false;
    }

    const QJsonObject obj = doc.object();
    const QString format = obj.value(QStringLiteral("format")).toString();
    const QString pdkName = obj.value(QStringLiteral("name")).toString(
        QFileInfo(m_pdkRoot).completeBaseName());

    m_tree->clear();
    auto *rootItem = new QTreeWidgetItem(m_tree);
    rootItem->setText(0, pdkName.isEmpty() ? QFileInfo(m_pdkRoot).fileName() : pdkName);
    rootItem->setToolTip(0, m_pdkRoot);
    stylePathItem(rootItem, QFileInfo(m_pdkRoot).isDir());

    if (!format.isEmpty() && format != QLatin1String("door-pdk")) {
        auto *fmtItem = new QTreeWidgetItem(rootItem);
        fmtItem->setText(0, tr("format: %1 (expected door-pdk)").arg(format));
        stylePathItem(fmtItem, false);
    }

    QSet<QString> exceptions;
    const QJsonArray exArr = obj.value(QStringLiteral("exceptions")).toArray();
    for (const QJsonValue &v : exArr) {
        if (v.isString()) {
            exceptions.insert(v.toString().trimmed());
        } else if (v.isObject()) {
            const QJsonObject eo = v.toObject();
            const QString n = eo.value(QStringLiteral("name")).toString().trimmed();
            if (!n.isEmpty()) {
                exceptions.insert(n);
            }
            const QString ex = eo.value(QStringLiteral("exception")).toString().trimmed();
            if (!ex.isEmpty()) {
                exceptions.insert(ex);
            }
        }
    }

    auto *libsNode = new QTreeWidgetItem(rootItem);
    libsNode->setText(0, tr("libraries (rules → resolved)"));
    const QJsonArray libs = obj.value(QStringLiteral("libraries")).toArray();
    for (const QJsonValue &v : libs) {
        const QJsonObject lo = v.toObject();
        const QString explicitName = lo.value(QStringLiteral("name")).toString();
        const QString rel = lo.value(QStringLiteral("path")).toString().trimmed();
        const QString role = lo.value(QStringLiteral("role")).toString();
        auto *ruleItem = new QTreeWidgetItem(libsNode);
        QString ruleLabel = rel;
        if (!role.isEmpty()) {
            ruleLabel += QStringLiteral("  [%1]").arg(role);
        }
        if (!explicitName.isEmpty() && !rel.contains(QLatin1Char('*'))) {
            ruleLabel = explicitName + QStringLiteral("  →  ") + ruleLabel;
        }
        ruleItem->setText(0, ruleLabel);

        if (rel.contains(QLatin1Char('*'))) {
            const int slash = rel.lastIndexOf(QLatin1Char('/'));
            const QString parentRel = slash >= 0 ? rel.left(slash) : QString();
            const QString glob = slash >= 0 ? rel.mid(slash + 1) : rel;
            const QString parentAbs =
                parentRel.isEmpty() ? m_pdkRoot : cleanJoin(m_pdkRoot, parentRel);
            const bool parentOk = QFileInfo(parentAbs).isDir();
            stylePathItem(ruleItem, parentOk);
            ruleItem->setToolTip(0, parentAbs);
            if (parentOk) {
                const QFileInfoList dirs = QDir(parentAbs).entryInfoList(
                    QStringList{glob}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                for (const QFileInfo &fi : dirs) {
                    const QString n = fi.fileName();
                    auto *child = new QTreeWidgetItem(ruleItem);
                    if (exceptions.contains(n)) {
                        child->setText(0, tr("%1  (exception — skipped)").arg(n));
                        stylePathItem(child, false);
                    } else {
                        child->setText(0, n);
                        stylePathItem(child, true);
                    }
                    child->setToolTip(0, fi.absoluteFilePath());
                }
                if (dirs.isEmpty()) {
                    auto *empty = new QTreeWidgetItem(ruleItem);
                    empty->setText(0, tr("(no matches)"));
                    stylePathItem(empty, false);
                }
            }
        } else {
            const QString abs = cleanJoin(m_pdkRoot, rel);
            const QString n =
                explicitName.isEmpty() ? QFileInfo(abs).fileName() : explicitName;
            const bool skipped = exceptions.contains(n);
            const bool ok = !skipped && QFileInfo(abs).isDir();
            stylePathItem(ruleItem, ok);
            ruleItem->setToolTip(0, abs);
            if (skipped) {
                ruleItem->setText(0, ruleLabel + tr("  (exception — skipped)"));
            }
        }
    }

    auto *exNode = new QTreeWidgetItem(rootItem);
    exNode->setText(0, tr("exceptions"));
    if (exceptions.isEmpty()) {
        auto *empty = new QTreeWidgetItem(exNode);
        empty->setText(0, tr("(none — user may add library folder names to skip)"));
    } else {
        QStringList names = exceptions.values();
        names.sort(Qt::CaseInsensitive);
        for (const QString &n : names) {
            auto *item = new QTreeWidgetItem(exNode);
            item->setText(0, n);
        }
    }

    auto *techNode = new QTreeWidgetItem(rootItem);
    techNode->setText(0, tr("tech"));
    const QJsonObject tech = obj.value(QStringLiteral("tech")).toObject();
    const QStringList techKeys = tech.keys();
    for (const QString &key : techKeys) {
        const QString rel = tech.value(key).toString();
        const QString abs = cleanJoin(m_pdkRoot, rel);
        const bool ok = !abs.isEmpty() && QFileInfo(abs).exists();
        auto *item = new QTreeWidgetItem(techNode);
        item->setText(0, QStringLiteral("%1  →  %2").arg(key, rel));
        item->setToolTip(0, abs);
        stylePathItem(item, ok);
    }

    m_tree->expandAll();
    return true;
}

void DoorPdkDescriptorDialog::applyTemplate(int index)
{
    if (index <= 0) {
        return;
    }
    if (m_editor->document()->isModified()
        || !m_editor->toPlainText().trimmed().isEmpty()) {
        const auto reply = QMessageBox::question(
            this,
            tr("Apply template"),
            tr("Replace the editor contents with the Open-PDK template?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            m_templateCombo->setCurrentIndex(0);
            return;
        }
    }
    const QString name = QFileInfo(m_pdkRoot).completeBaseName();
    m_updatingEditor = true;
    m_editor->setPlainText(openPdkTemplateJson(name));
    m_editor->document()->setModified(true);
    m_updatingEditor = false;
    m_templateCombo->setCurrentIndex(0);
    refreshPathTree();
}

void DoorPdkDescriptorDialog::saveDescriptor()
{
    QJsonParseError err;
    const QByteArray raw = m_editor->toPlainText().toUtf8();
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, tr("Save"),
                             tr("Cannot save: invalid JSON.\n%1").arg(err.errorString()));
        return;
    }
    const QJsonObject obj = doc.object();
    const QString format = obj.value(QStringLiteral("format")).toString();
    if (format != QLatin1String("door-pdk")) {
        QMessageBox::warning(this, tr("Save"),
                             tr("Cannot save: \"format\" must be \"door-pdk\"."));
        return;
    }

    // Pretty-print on save for stable diffs.
    const QByteArray out = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    QFileInfo fi(m_descriptorPath);
    if (!QDir().mkpath(fi.absolutePath())) {
        QMessageBox::warning(this, tr("Save"),
                             tr("Cannot create directory:\n%1").arg(fi.absolutePath()));
        return;
    }
    QFile f(m_descriptorPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save"),
                             tr("Cannot write:\n%1\n%2").arg(m_descriptorPath, f.errorString()));
        return;
    }
    f.write(out);
    f.close();

    m_updatingEditor = true;
    m_editor->setPlainText(QString::fromUtf8(out));
    m_editor->document()->setModified(false);
    m_updatingEditor = false;
    m_saved = true;
    m_statusLabel->setText(tr("Saved."));
    m_statusLabel->setStyleSheet(QStringLiteral("color: #1a7f37;"));
    refreshPathTree();
}

void DoorPdkDescriptorDialog::reloadFromDisk()
{
    if (m_editor->document()->isModified()) {
        const auto reply = QMessageBox::question(
            this,
            tr("Reload"),
            tr("Discard unsaved changes and reload from disk?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }
    loadFile();
}
