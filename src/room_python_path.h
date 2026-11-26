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
 * \brief One-shot copy of legacy ``LIBAMN`` QSettings into ``DOOR`` when the new store is empty.
 **********************************************************************************************************************/
inline void migrateLibamnSettingsToDoor()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    QSettings door(QStringLiteral("DOOR"));
    if (!door.allKeys().isEmpty()) {
        return;
    }

    QSettings legacy(QStringLiteral("LIBAMN"));
    const QStringList keys = legacy.allKeys();
    if (keys.isEmpty()) {
        return;
    }
    for (const QString &key : keys) {
        door.setValue(key, legacy.value(key));
    }
    door.sync();
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
