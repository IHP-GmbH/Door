#ifndef OASREADER_H
#define OASREADER_H

#include <QString>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QByteArray>

#include <functional>

struct LayoutHierarchy
{
    QStringList                 topCells;
    QHash<QString, QStringList> children;
    QSet<QString>               allCells;
};

class oasReader
{
public:
    using ProgressFn = std::function<void(const QString &cellName)>;
    using RenameFn = std::function<void(const QString &from, const QString &to)>;

    explicit                    oasReader(const QString &fileName);

    bool                        readHierarchy(LayoutHierarchy &out,
                                              const ProgressFn &onCell = {},
                                              const RenameFn &onRename = {});
    QStringList                 getErrors() const;

    void                        oasCreate(const QString &cellName);

private:
    void                        writeRaw(const QByteArray &data) const;
    void                        writeByte(uchar value) const;
    void                        writeUInt(quint64 value) const;
    void                        writeSInt(qint64 value) const;
    void                        writeString(const QByteArray &value) const;
    void                        writeAString(const QString &value) const;
    void                        writeNString(const QString &value) const;

    void                        writeMagic() const;
    void                        writeStartRecord() const;
    void                        writeCellRecord(const QString &cellName) const;
    void                        writeEndRecord() const;

private:
    QString                     m_fileName;
    mutable QStringList         m_errorList;
};

#endif // OASREADER_H
