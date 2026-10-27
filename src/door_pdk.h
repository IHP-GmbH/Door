#ifndef DOOR_PDK_H
#define DOOR_PDK_H

#include <QMap>
#include <QString>
#include <QStringList>

/*! Descriptor file name written / read at the PDK root. */
inline QString doorPdkDescriptorFileName()
{
    return QStringLiteral(".door-pdk.json");
}

struct DoorPdkLibrary
{
    QString name;   /*!< Library name shown in the Projects tree. */
    QString path;   /*!< Absolute path to library root. */
    QString role;   /*!< Optional role: primitives, stdcell, schematic, … */
};

struct DoorPdkTech
{
    QString root;     /*!< libs.tech */
    QString klayout;  /*!< libs.tech/klayout */
    QString layers;   /*!< layer maps / .lyp/.lyt */
    QString drc;      /*!< DRC scripts / rules */
    QString lvs;      /*!< LVS scripts / rules */
    QString xschem;
    QString ngspice;
    QString qucs;
};

struct DoorPdkInfo
{
    QString name;              /*!< PDK id, typically folder basename (ihp-sg13g2). */
    QString rootPath;          /*!< Absolute PDK root directory. */
    QString descriptorPath;    /*!< Path to .door-pdk.json if used; empty if auto-detected. */
    QList<DoorPdkLibrary> libraries;
    DoorPdkTech tech;
    bool ok = false;
    QString error;
};

/*!
 * Load PDK layout from \a pdkRoot.
 * Prefer `.door-pdk.json` when present; otherwise auto-detect Open-PDK layout
 * (`libs.ref/*`, `libs.tech/...`).
 * \a preferredName overrides the display/id name when non-empty.
 */
DoorPdkInfo loadDoorPdk(const QString &pdkRoot, const QString &preferredName = QString());

/*!
 * Write a portable `.door-pdk.json` (glob rules like libs.ref/*, empty exceptions,
 * relative tech paths). Does not hard-code cell-library names.
 */
bool writeDoorPdkDescriptor(const DoorPdkInfo &info, QString *errorOut = nullptr);

/*!
 * Portable Open-PDK descriptor skeleton:
 * libraries: [{ "path": "libs.ref/*" }, …], exceptions: [] (user fills exclusions).
 * Drop next to libs.ref / libs.tech; names come from directories on disk.
 */
QString openPdkTemplateJson(const QString &pdkName = QString());

#endif // DOOR_PDK_H
