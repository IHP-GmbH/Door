#ifndef ROOM_PYTHON_PATH_H
#define ROOM_PYTHON_PATH_H

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QtGlobal>

/*!*********************************************************************************************************************
 * \brief Resolves a user-configured Room path to the directory that contains ``room/__init__.py``.
 *
 * Accepts either ``…/Room/python`` or the Room checkout root ``…/Room`` (then appends ``/python``).
 **********************************************************************************************************************/
inline QString roomPackageDirFromConfiguredPath(const QString &configured)
{
    const QString path = configured.trimmed();
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo fi(path);
    if (!fi.exists()) {
        return {};
    }
    const QString abs = fi.absoluteFilePath();
    if (QFileInfo::exists(QDir(abs).filePath(QStringLiteral("room/__init__.py")))) {
        return abs;
    }
    const QString asPython = QDir(abs).filePath(QStringLiteral("python"));
    if (QFileInfo::exists(QDir(asPython).filePath(QStringLiteral("room/__init__.py")))) {
        return QFileInfo(asPython).absoluteFilePath();
    }
    return {};
}

/*!*********************************************************************************************************************
 * \brief Copy missing legacy ``LIBAMN`` QSettings keys into ``DOOR`` (partial stores included).
 *
 * Door may create a non-empty ``DOOR`` store (e.g. Tools/RoomPath) before RecentProjects are
 * migrated; only skipping when ``DOOR`` is completely empty would drop the recent-project list.
 **********************************************************************************************************************/
inline void migrateLibamnSettingsToDoor()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    QSettings legacy(QStringLiteral("LIBAMN"));
    const QStringList keys = legacy.allKeys();
    if (keys.isEmpty()) {
        return;
    }

    QSettings door(QStringLiteral("DOOR"));
    bool changed = false;
    for (const QString &key : keys) {
        const QVariant legacyValue = legacy.value(key);
        if (!door.contains(key)) {
            door.setValue(key, legacyValue);
            changed = true;
            continue;
        }
        // Recover empty recent list if DOOR was created without it.
        if (key == QLatin1String("RecentProjects/RecentProjList")
            && door.value(key).toStringList().isEmpty()
            && !legacyValue.toStringList().isEmpty()) {
            door.setValue(key, legacyValue);
            changed = true;
        }
    }
    if (changed) {
        door.sync();
    }
}

/*!*********************************************************************************************************************
 * \brief Reads Tools/RoomPath from the same QSettings store as MainWindow (``DOOR``).
 **********************************************************************************************************************/
inline QString configuredRoomPathFromSettings()
{
    migrateLibamnSettingsToDoor();
    QSettings settings(QStringLiteral("DOOR"));
    settings.beginGroup(QStringLiteral("Tools"));
    return settings.value(QStringLiteral("RoomPath")).toString().trimmed();
}

/*!*********************************************************************************************************************
 * \brief Auto-detect Room's python package dir (env, bundled next to door, sibling / .deps Room).
 *
 * Does **not** read Tools/RoomPath — use \c resolveRoomPackageDir() for the full preference order.
 **********************************************************************************************************************/
inline QString discoverRoomPackageDir(const QString &appDirHint = QString())
{
    for (const char *key : {"DOOR_ROOM_PYTHON", "LIBMAN_ROOM_PYTHON"}) {
        const QString env = roomPackageDirFromConfiguredPath(qEnvironmentVariable(key));
        if (!env.isEmpty()) {
            return env;
        }
    }

    const QString appDir =
        appDirHint.isEmpty() ? QCoreApplication::applicationDirPath() : appDirHint;

    QStringList candidates;
    candidates << QDir(appDir).filePath(QStringLiteral("python"));
    candidates << QDir::cleanPath(QDir(appDir).filePath(QStringLiteral("../python")));

    QDir walk(appDir);
    for (int i = 0; i < 8; ++i) {
        candidates << walk.filePath(QStringLiteral("Room/python"));
        candidates << QFileInfo(walk.filePath(QStringLiteral("../Room/python"))).absoluteFilePath();
        candidates << walk.filePath(QStringLiteral(".deps/Room/python"));
        if (!walk.cdUp()) {
            break;
        }
    }

    QStringList seen;
    for (const QString &c : candidates) {
        const QString abs = QDir(c).absolutePath();
        if (seen.contains(abs) || !QFileInfo::exists(abs)) {
            continue;
        }
        seen << abs;
        if (QFileInfo::exists(QDir(abs).filePath(QStringLiteral("room/__init__.py")))) {
            return abs;
        }
    }
    return {};
}

/*!*********************************************************************************************************************
 * \brief Tools/RoomPath → env → bundled/sibling discovery.
 **********************************************************************************************************************/
inline QString resolveRoomPackageDir(const QString &appDirHint = QString())
{
    const QString fromSettings =
        roomPackageDirFromConfiguredPath(configuredRoomPathFromSettings());
    if (!fromSettings.isEmpty()) {
        return fromSettings;
    }
    return discoverRoomPackageDir(appDirHint);
}

#endif // ROOM_PYTHON_PATH_H
