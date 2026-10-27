#include "door_pdk.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QSet>

namespace {

QString cleanAbs(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

QString joinUnder(const QString &root, const QString &rel)
{
    if (root.isEmpty() || rel.isEmpty()) {
        return {};
    }
    if (QDir::isAbsolutePath(rel)) {
        return cleanAbs(rel);
    }
    return cleanAbs(root + QLatin1Char('/') + rel);
}

bool dirExists(const QString &path)
{
    return !path.isEmpty() && QFileInfo(path).isDir();
}

bool looksLikeLibraryDir(const QString &absPath)
{
    const QDir libDir(absPath);
    return libDir.exists(QStringLiteral("gds"))
        || libDir.exists(QStringLiteral("lef"))
        || libDir.exists(QStringLiteral("cdl"))
        || libDir.exists(QStringLiteral("lib"))
        || libDir.exists(QStringLiteral("spice"))
        || !libDir.entryList(QStringList() << QStringLiteral("*.room")
                                           << QStringLiteral("*.gds")
                                           << QStringLiteral("*.sch")
                                           << QStringLiteral("*.sym"),
                             QDir::Files | QDir::Dirs).isEmpty();
}

bool isScratchDirName(const QString &n)
{
    return n.startsWith(QLatin1Char('.'))
        || n.startsWith(QStringLiteral("New "), Qt::CaseInsensitive)
        || n.compare(QStringLiteral("tmp"), Qt::CaseInsensitive) == 0;
}

QString inferRoleFromName(const QString &n, const QString &fallback = QStringLiteral("ref"))
{
    if (n.contains(QStringLiteral("stdcell"), Qt::CaseInsensitive)) {
        return QStringLiteral("stdcell");
    }
    if (n.endsWith(QStringLiteral("_pr")) || n.contains(QStringLiteral("prim"))) {
        return QStringLiteral("primitives");
    }
    if (n.contains(QStringLiteral("_io"), Qt::CaseInsensitive)) {
        return QStringLiteral("io");
    }
    if (n.contains(QStringLiteral("sram"), Qt::CaseInsensitive)) {
        return QStringLiteral("sram");
    }
    return fallback;
}

void addLibIfDir(QList<DoorPdkLibrary> *libs,
                 QSet<QString> *seen,
                 const QString &name,
                 const QString &absPath,
                 const QString &role)
{
    if (!libs || name.isEmpty() || !dirExists(absPath)) {
        return;
    }
    if (seen && seen->contains(name)) {
        return;
    }
    DoorPdkLibrary lib;
    lib.name = name;
    lib.path = absPath;
    lib.role = role;
    libs->append(lib);
    if (seen) {
        seen->insert(name);
    }
}

DoorPdkTech autoDetectTech(const QString &root)
{
    DoorPdkTech tech;
    tech.root = joinUnder(root, QStringLiteral("libs.tech"));
    if (!dirExists(tech.root)) {
        tech.root.clear();
        return tech;
    }

    tech.klayout = joinUnder(tech.root, QStringLiteral("klayout"));
    const QString techDir = joinUnder(tech.klayout, QStringLiteral("tech"));
    if (dirExists(techDir)) {
        tech.layers = techDir;
        const QString drc = joinUnder(techDir, QStringLiteral("drc"));
        const QString lvs = joinUnder(techDir, QStringLiteral("lvs"));
        tech.drc = dirExists(drc) ? drc : techDir;
        tech.lvs = dirExists(lvs) ? lvs : techDir;
    } else if (dirExists(tech.klayout)) {
        tech.layers = tech.klayout;
        tech.drc = tech.klayout;
        tech.lvs = tech.klayout;
    }

    const QString xschem = joinUnder(tech.root, QStringLiteral("xschem"));
    if (dirExists(xschem)) {
        tech.xschem = xschem;
    }
    const QString ngspice = joinUnder(tech.root, QStringLiteral("ngspice"));
    if (dirExists(ngspice)) {
        tech.ngspice = ngspice;
    }
    const QString qucs = joinUnder(tech.root, QStringLiteral("qucs-s"));
    if (dirExists(qucs)) {
        tech.qucs = qucs;
    }
    return tech;
}

QSet<QString> parseExceptions(const QJsonArray &arr)
{
    QSet<QString> out;
    for (const QJsonValue &v : arr) {
        if (v.isString()) {
            const QString n = v.toString().trimmed();
            if (!n.isEmpty()) {
                out.insert(n);
            }
            continue;
        }
        if (v.isObject()) {
            const QString n = v.toObject().value(QStringLiteral("name")).toString().trimmed();
            if (!n.isEmpty()) {
                out.insert(n);
            }
            // Also accept "exception": "sg13g2_io" inside an object.
            const QString ex = v.toObject().value(QStringLiteral("exception")).toString().trimmed();
            if (!ex.isEmpty()) {
                out.insert(ex);
            }
        }
    }
    return out;
}

/*! Expand a relative path that may contain a single-segment glob (* or *_tests). */
QList<QPair<QString, QString>> expandPathPattern(const QString &root, const QString &pattern)
{
    QList<QPair<QString, QString>> matches; // name, absPath
    const QString rel = QDir::fromNativeSeparators(pattern.trimmed());
    if (rel.isEmpty()) {
        return matches;
    }

    if (!rel.contains(QLatin1Char('*'))) {
        const QString abs = joinUnder(root, rel);
        if (dirExists(abs)) {
            matches.append({QFileInfo(abs).fileName(), abs});
        }
        return matches;
    }

    // Only support a glob in the last path segment: parent/glob
    const int slash = rel.lastIndexOf(QLatin1Char('/'));
    const QString parentRel = slash >= 0 ? rel.left(slash) : QString();
    const QString glob = slash >= 0 ? rel.mid(slash + 1) : rel;
    const QString parentAbs = parentRel.isEmpty() ? root : joinUnder(root, parentRel);
    if (!dirExists(parentAbs)) {
        return matches;
    }

    const QFileInfoList dirs =
        QDir(parentAbs).entryInfoList(QStringList{glob}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &fi : dirs) {
        matches.append({fi.fileName(), cleanAbs(fi.absoluteFilePath())});
    }
    return matches;
}

DoorPdkInfo autoDetectOpenPdk(const QString &root, const QString &name)
{
    DoorPdkInfo info;
    info.rootPath = cleanAbs(root);
    info.name = name.isEmpty() ? QFileInfo(info.rootPath).completeBaseName() : name;
    info.tech = autoDetectTech(info.rootPath);

    QSet<QString> seen;
    const QString refRoot = joinUnder(info.rootPath, QStringLiteral("libs.ref"));
    if (dirExists(refRoot)) {
        const QFileInfoList dirs =
            QDir(refRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &fi : dirs) {
            const QString n = fi.fileName();
            if (isScratchDirName(n) || !looksLikeLibraryDir(fi.absoluteFilePath())) {
                continue;
            }
            addLibIfDir(&info.libraries, &seen, n, fi.absoluteFilePath(), inferRoleFromName(n));
        }
    }

    // Schematic / example bundles under libs.tech (shown as project libs).
    addLibIfDir(&info.libraries,
                &seen,
                QStringLiteral("examples"),
                joinUnder(info.rootPath, QStringLiteral("libs.tech/qucs-s/examples")),
                QStringLiteral("schematic"));
    const auto xschemMatches =
        expandPathPattern(info.rootPath, QStringLiteral("libs.tech/xschem/*"));
    for (const auto &m : xschemMatches) {
        if (isScratchDirName(m.first) || !looksLikeLibraryDir(m.second)) {
            continue;
        }
        addLibIfDir(&info.libraries, &seen, m.first, m.second, QStringLiteral("schematic"));
    }

    info.ok = !info.libraries.isEmpty() || dirExists(info.tech.root);
    if (!info.ok) {
        info.error = QStringLiteral(
            "Directory does not look like an Open-PDK (missing libs.ref / libs.tech).");
    }
    return info;
}

DoorPdkInfo loadFromDescriptor(const QString &root, const QString &descPath, const QString &preferredName)
{
    DoorPdkInfo info;
    info.rootPath = cleanAbs(root);
    info.descriptorPath = cleanAbs(descPath);

    QFile f(descPath);
    if (!f.open(QIODevice::ReadOnly)) {
        info.error = QStringLiteral("Cannot read %1").arg(descPath);
        return info;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        info.error = QStringLiteral("Invalid JSON in %1: %2").arg(descPath, err.errorString());
        return info;
    }

    const QJsonObject obj = doc.object();
    info.name = preferredName.isEmpty()
        ? obj.value(QStringLiteral("name")).toString(QFileInfo(info.rootPath).completeBaseName())
        : preferredName;

    const QSet<QString> exceptions = parseExceptions(obj.value(QStringLiteral("exceptions")).toArray());

    QSet<QString> seen;
    const QJsonArray libs = obj.value(QStringLiteral("libraries")).toArray();
    for (const QJsonValue &v : libs) {
        const QJsonObject lo = v.toObject();
        const QString rel = lo.value(QStringLiteral("path")).toString().trimmed();
        if (rel.isEmpty()) {
            continue;
        }
        const QString ruleRole = lo.value(QStringLiteral("role")).toString().trimmed();
        // libs.tech is agent-only — never a Project-tree library via role=tech.
        if (ruleRole.compare(QStringLiteral("tech"), Qt::CaseInsensitive) == 0) {
            continue;
        }

        const bool isGlob = rel.contains(QLatin1Char('*'));
        const QString explicitName = lo.value(QStringLiteral("name")).toString().trimmed();

        // Legacy explicit entry (no glob): keep name/path/role as written.
        if (!isGlob) {
            const QString abs = joinUnder(info.rootPath, rel);
            QString name = explicitName;
            if (name.isEmpty()) {
                name = QFileInfo(abs).fileName();
            }
            if (name.compare(QStringLiteral("libs.tech"), Qt::CaseInsensitive) == 0
                || name.startsWith(QStringLiteral("New "), Qt::CaseInsensitive)
                || exceptions.contains(name)) {
                continue;
            }
            const QString role = ruleRole.isEmpty() ? inferRoleFromName(name) : ruleRole;
            addLibIfDir(&info.libraries, &seen, name, abs, role);
            continue;
        }

        // Rule with glob: expand children; names come from directories.
        const auto matches = expandPathPattern(info.rootPath, rel);
        for (const auto &m : matches) {
            const QString &name = m.first;
            const QString &abs = m.second;
            if (exceptions.contains(name) || isScratchDirName(name)) {
                continue;
            }
            if (name.compare(QStringLiteral("libs.tech"), Qt::CaseInsensitive) == 0) {
                continue;
            }
            // Under libs.ref prefer library-looking dirs; under tech examples keep sch/sym dirs.
            const bool underRef = QDir::fromNativeSeparators(rel)
                                      .startsWith(QStringLiteral("libs.ref/"));
            if (underRef && !looksLikeLibraryDir(abs)) {
                continue;
            }
            if (!underRef && !looksLikeLibraryDir(abs) && !dirExists(abs)) {
                continue;
            }
            const QString role = ruleRole.isEmpty() ? inferRoleFromName(name) : ruleRole;
            addLibIfDir(&info.libraries, &seen, name, abs, role);
        }
    }

    const QJsonObject techObj = obj.value(QStringLiteral("tech")).toObject();
    auto techPath = [&](const char *key) -> QString {
        const QString rel = techObj.value(QLatin1String(key)).toString().trimmed();
        if (rel.isEmpty()) {
            return {};
        }
        const QString abs = joinUnder(info.rootPath, rel);
        return dirExists(abs) ? abs : QString();
    };
    info.tech.root = techPath("root");
    info.tech.klayout = techPath("klayout");
    info.tech.layers = techPath("layers");
    info.tech.drc = techPath("drc");
    info.tech.lvs = techPath("lvs");
    info.tech.xschem = techPath("xschem");
    info.tech.ngspice = techPath("ngspice");
    info.tech.qucs = techPath("qucs");

    const DoorPdkTech detected = autoDetectTech(info.rootPath);
    if (info.tech.root.isEmpty()) {
        info.tech.root = detected.root;
    }
    if (info.tech.klayout.isEmpty()) {
        info.tech.klayout = detected.klayout;
    }
    if (info.tech.layers.isEmpty()) {
        info.tech.layers = detected.layers;
    }
    if (info.tech.drc.isEmpty()) {
        info.tech.drc = detected.drc;
    }
    if (info.tech.lvs.isEmpty()) {
        info.tech.lvs = detected.lvs;
    }
    if (info.tech.xschem.isEmpty()) {
        info.tech.xschem = detected.xschem;
    }
    if (info.tech.ngspice.isEmpty()) {
        info.tech.ngspice = detected.ngspice;
    }
    if (info.tech.qucs.isEmpty()) {
        info.tech.qucs = detected.qucs;
    }

    info.ok = !info.libraries.isEmpty() || dirExists(info.tech.root);
    if (!info.ok) {
        info.error = QStringLiteral("Descriptor %1 lists no usable libraries.").arg(descPath);
    }
    return info;
}

QString relativeToRoot(const QString &root, const QString &abs)
{
    if (root.isEmpty() || abs.isEmpty()) {
        return {};
    }
    return QDir(root).relativeFilePath(abs);
}

QJsonObject portableLibrariesJson(const QString &root)
{
    QJsonArray libs;
    auto addRule = [&](const QString &path, const QString &role = QString()) {
        QJsonObject o;
        o.insert(QStringLiteral("path"), path);
        if (!role.isEmpty()) {
            o.insert(QStringLiteral("role"), role);
        }
        libs.append(o);
    };

    if (dirExists(joinUnder(root, QStringLiteral("libs.ref"))) || root.isEmpty()) {
        addRule(QStringLiteral("libs.ref/*"));
    }
    if (root.isEmpty() || dirExists(joinUnder(root, QStringLiteral("libs.tech/qucs-s/examples")))) {
        addRule(QStringLiteral("libs.tech/qucs-s/examples"), QStringLiteral("schematic"));
    }
    if (root.isEmpty() || dirExists(joinUnder(root, QStringLiteral("libs.tech/xschem")))) {
        addRule(QStringLiteral("libs.tech/xschem/*"), QStringLiteral("schematic"));
    }

    QJsonObject wrap;
    wrap.insert(QStringLiteral("libraries"), libs);
    wrap.insert(QStringLiteral("exceptions"), QJsonArray());
    return wrap;
}

} // namespace

DoorPdkInfo loadDoorPdk(const QString &pdkRoot, const QString &preferredName)
{
    DoorPdkInfo info;
    const QString root = cleanAbs(pdkRoot);
    if (!dirExists(root)) {
        info.error = QStringLiteral("PDK path does not exist: %1").arg(pdkRoot);
        return info;
    }

    const QString desc = joinUnder(root, doorPdkDescriptorFileName());
    if (QFileInfo::exists(desc)) {
        return loadFromDescriptor(root, desc, preferredName);
    }
    return autoDetectOpenPdk(root, preferredName);
}

bool writeDoorPdkDescriptor(const DoorPdkInfo &info, QString *errorOut)
{
    if (info.rootPath.isEmpty() || !dirExists(info.rootPath)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Invalid PDK root.");
        }
        return false;
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("format"), QStringLiteral("door-pdk"));
    obj.insert(QStringLiteral("version"), 1);
    obj.insert(QStringLiteral("name"), info.name);

    // Portable rules — never enumerate hard-coded cell-library names.
    const QJsonObject portable = portableLibrariesJson(info.rootPath);
    obj.insert(QStringLiteral("libraries"), portable.value(QStringLiteral("libraries")));
    obj.insert(QStringLiteral("exceptions"), portable.value(QStringLiteral("exceptions")));

    QJsonObject tech;
    auto put = [&](const char *key, const QString &abs) {
        if (!abs.isEmpty()) {
            tech.insert(QLatin1String(key), relativeToRoot(info.rootPath, abs));
        }
    };
    put("root", info.tech.root);
    put("klayout", info.tech.klayout);
    put("layers", info.tech.layers);
    put("drc", info.tech.drc);
    put("lvs", info.tech.lvs);
    put("xschem", info.tech.xschem);
    put("ngspice", info.tech.ngspice);
    put("qucs", info.tech.qucs);
    if (tech.isEmpty()) {
        // Prefer relative Open-PDK defaults when tech was empty.
        tech.insert(QStringLiteral("root"), QStringLiteral("libs.tech"));
        tech.insert(QStringLiteral("klayout"), QStringLiteral("libs.tech/klayout"));
        tech.insert(QStringLiteral("layers"), QStringLiteral("libs.tech/klayout/tech"));
        tech.insert(QStringLiteral("drc"), QStringLiteral("libs.tech/klayout/tech/drc"));
        tech.insert(QStringLiteral("lvs"), QStringLiteral("libs.tech/klayout/tech/lvs"));
        tech.insert(QStringLiteral("ngspice"), QStringLiteral("libs.tech/ngspice"));
        tech.insert(QStringLiteral("qucs"), QStringLiteral("libs.tech/qucs-s"));
        tech.insert(QStringLiteral("xschem"), QStringLiteral("libs.tech/xschem"));
    }
    obj.insert(QStringLiteral("tech"), tech);

    const QString outPath = joinUnder(info.rootPath, doorPdkDescriptorFileName());
    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorOut) {
            *errorOut = f.errorString();
        }
        return false;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

QString openPdkTemplateJson(const QString &pdkName)
{
    const QString name = pdkName.trimmed().isEmpty() ? QStringLiteral("open-pdk") : pdkName.trimmed();

    QJsonObject obj;
    obj.insert(QStringLiteral("format"), QStringLiteral("door-pdk"));
    obj.insert(QStringLiteral("version"), 1);
    obj.insert(QStringLiteral("name"), name);

    const QJsonObject portable = portableLibrariesJson(QString());
    obj.insert(QStringLiteral("libraries"), portable.value(QStringLiteral("libraries")));
    obj.insert(QStringLiteral("exceptions"), QJsonArray());

    QJsonObject tech;
    tech.insert(QStringLiteral("root"), QStringLiteral("libs.tech"));
    tech.insert(QStringLiteral("klayout"), QStringLiteral("libs.tech/klayout"));
    tech.insert(QStringLiteral("layers"), QStringLiteral("libs.tech/klayout/tech"));
    tech.insert(QStringLiteral("drc"), QStringLiteral("libs.tech/klayout/tech/drc"));
    tech.insert(QStringLiteral("lvs"), QStringLiteral("libs.tech/klayout/tech/lvs"));
    tech.insert(QStringLiteral("ngspice"), QStringLiteral("libs.tech/ngspice"));
    tech.insert(QStringLiteral("qucs"), QStringLiteral("libs.tech/qucs-s"));
    tech.insert(QStringLiteral("xschem"), QStringLiteral("libs.tech/xschem"));
    obj.insert(QStringLiteral("tech"), tech);

    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}
