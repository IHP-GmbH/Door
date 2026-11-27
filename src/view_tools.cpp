#include "view_tools.h"

#include "property.h"
#include "toolpickerdialog.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWidget>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <shlobj.h>
#endif

namespace {

QVector<ViewToolEntry> migrateLegacyTool(const Properties *properties, const QString &groupName)
{
    QVector<ViewToolEntry> entries;
    if (!properties || !properties->exists(groupName)) {
        return entries;
    }

    ViewToolEntry entry;
    entry.name = groupName;
    entry.path = properties->get<QString>(groupName);
    entry.isDefault = true;
    if (!entry.path.trimmed().isEmpty()) {
        entries.push_back(entry);
    }
    return entries;
}

QVector<ViewToolEntry> parseToolsJson(const QString &jsonText)
{
    QVector<ViewToolEntry> entries;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
    if (!doc.isArray()) {
        return entries;
    }

    for (const QJsonValue &value : doc.array()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        ViewToolEntry entry;
        entry.name = obj.value(QStringLiteral("name")).toString().trimmed();
        entry.path = obj.value(QStringLiteral("path")).toString().trimmed();
        entry.isDefault = obj.value(QStringLiteral("default")).toBool(false);
        if (!entry.path.isEmpty()) {
            entries.push_back(entry);
        }
    }
    return entries;
}

QString encodeToolsJson(const QVector<ViewToolEntry> &entries)
{
    QJsonArray array;
    for (const ViewToolEntry &entry : entries) {
        if (entry.path.trimmed().isEmpty()) {
            continue;
        }
        QJsonObject obj;
        obj.insert(QStringLiteral("name"), entry.name.trimmed().isEmpty() ? QStringLiteral("Tool") : entry.name.trimmed());
        obj.insert(QStringLiteral("path"), entry.path.trimmed());
        obj.insert(QStringLiteral("default"), entry.isDefault);
        array.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

} // namespace

QString viewToolsPropertyKey(const QString &groupName)
{
    return groupName.trimmed() + QStringLiteral("Tools");
}

QVector<ViewToolEntry> loadViewTools(const Properties *properties, const QString &groupName)
{
    if (!properties) {
        return {};
    }

    const QString key = viewToolsPropertyKey(groupName);
    if (properties->exists(key)) {
        const QVector<ViewToolEntry> parsed = parseToolsJson(properties->get<QString>(key));
        if (!parsed.isEmpty()) {
            return parsed;
        }
    }

    return migrateLegacyTool(properties, groupName);
}

void saveViewTools(Properties *properties, const QString &groupName, const QVector<ViewToolEntry> &entries)
{
    if (!properties) {
        return;
    }

    const QString key = viewToolsPropertyKey(groupName);
    const QString json = encodeToolsJson(entries);
    if (json.isEmpty() || json == QStringLiteral("[]")) {
        properties->remove(key);
    } else {
        properties->set(key, json);
    }
}

QString resolveViewToolPath(const QVector<ViewToolEntry> &entries, QWidget *parent)
{
    QVector<ViewToolEntry> usable;
    usable.reserve(entries.size());
    for (const ViewToolEntry &entry : entries) {
        if (!entry.path.trimmed().isEmpty()) {
            usable.push_back(entry);
        }
    }

    if (usable.isEmpty()) {
        return {};
    }
    if (usable.size() == 1) {
        return usable.front().path;
    }

    int defaultCount = 0;
    int defaultIndex = -1;
    for (int i = 0; i < usable.size(); ++i) {
        if (usable.at(i).isDefault) {
            ++defaultCount;
            defaultIndex = i;
        }
    }

    if (defaultCount == 1 && defaultIndex >= 0) {
        return usable.at(defaultIndex).path;
    }

    ToolPickerDialog dialog(usable, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    return dialog.selectedPath();
}

QString resolveToolLaunchPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return trimmed;
    }

#if defined(Q_OS_WIN)
    if (!trimmed.endsWith(QLatin1String(".lnk"), Qt::CaseInsensitive)) {
        return trimmed;
    }

    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comOwned = SUCCEEDED(comHr);
    QString resolved = trimmed;

    IShellLinkW *shellLink = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                   reinterpret_cast<void **>(&shellLink)))
        && shellLink) {
        IPersistFile *persist = nullptr;
        if (SUCCEEDED(shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&persist)))
            && persist) {
            if (SUCCEEDED(persist->Load(reinterpret_cast<LPCWSTR>(trimmed.utf16()), STGM_READ))) {
                wchar_t target[MAX_PATH] = {};
                WIN32_FIND_DATAW findData = {};
                if (SUCCEEDED(shellLink->GetPath(target, MAX_PATH, &findData, SLGP_UNCPRIORITY))) {
                    const QString candidate = QString::fromWCharArray(target).trimmed();
                    if (!candidate.isEmpty()) {
                        resolved = candidate;
                    }
                }
            }
            persist->Release();
        }
        shellLink->Release();
    }

    if (comOwned) {
        CoUninitialize();
    }
    return resolved;
#else
    Q_UNUSED(trimmed);
    return path.trimmed();
#endif
}
