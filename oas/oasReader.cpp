// oasReader.cpp — hierarchy reads go through CommonDB's room::OasReader (oas_to_room).
#include "oasReader.h"

#ifndef LIBMAN_NO_ROOM
#include "oas_reader.h"
#endif

oasReader::oasReader(const QString &fileName)
    : m_fileName(fileName)
{
    m_errorList.clear();
}

QStringList oasReader::getErrors() const
{
    return m_errorList;
}

bool oasReader::readHierarchy(LayoutHierarchy &out,
                              const ProgressFn &onCell,
                              const RenameFn &onRename)
{
    out.topCells.clear();
    out.children.clear();
    out.allCells.clear();
    m_errorList.clear();

    if (m_fileName.isEmpty()) {
        m_errorList << QStringLiteral("Empty OASIS filename.");
        return false;
    }

#ifndef LIBMAN_NO_ROOM
    room::OasReader reader(m_fileName.toStdString());
    room::OasHierarchy hierarchy;

    room::OasReader::ProgressFn roomProgress;
    if (onCell) {
        roomProgress = [&onCell](const std::string &cellName) {
            onCell(QString::fromStdString(cellName));
        };
    }

    room::OasReader::RenameFn roomRename;
    if (onRename) {
        roomRename = [&onRename](const std::string &from, const std::string &to) {
            onRename(QString::fromStdString(from), QString::fromStdString(to));
        };
    }

    if (!reader.readHierarchy(hierarchy, roomProgress, roomRename)) {
        for (const std::string &err : reader.errors()) {
            m_errorList << QString::fromStdString(err);
        }
        if (m_errorList.isEmpty()) {
            m_errorList << QStringLiteral("OASIS hierarchy parse failed: '%1'").arg(m_fileName);
        }
        return false;
    }

    out.topCells.reserve(static_cast<int>(hierarchy.topCells.size()));
    for (const std::string &top : hierarchy.topCells) {
        out.topCells << QString::fromStdString(top);
    }

    for (const std::string &cell : hierarchy.allCells) {
        out.allCells.insert(QString::fromStdString(cell));
    }

    for (const auto &entry : hierarchy.children) {
        QStringList children;
        children.reserve(static_cast<int>(entry.second.size()));
        for (const std::string &child : entry.second) {
            children << QString::fromStdString(child);
        }
        out.children.insert(QString::fromStdString(entry.first), children);
    }

    return true;
#else
    Q_UNUSED(onCell);
    Q_UNUSED(onRename);
    m_errorList << QStringLiteral("OASIS hierarchy requires ROOM (CommonDB) support.");
    return false;
#endif
}
