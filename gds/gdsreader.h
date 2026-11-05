// gdsreader.h
#ifndef GDSREADER_H
#define GDSREADER_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>

#include <functional>

class GdsReader
{
public:
    struct GdsHierarchy {
        QStringList topCells;
        QMap<QString, QStringList> children;
        QSet<QString> allCells;
    };

    using ProgressFn = std::function<void(const QString &cellName)>;

public:
    explicit GdsReader(const QString &fileName);

    void        gdsCreate(const QString &cellName);
    QStringList getErrors() const { return m_errorList; }

    bool        readHierarchy(GdsHierarchy &out, const ProgressFn &onCell = {});

private:
    void        gdsBegin(const QString &libName) const;
    void        gdsEnd() const;
    int*        gdsTime() const;

    int         writeInt(int record, int arrInt[], int cnt) const;
    int         writeStr(int record, std::string inStr) const;
    void        writeUnits() const;
    int         writeRec(int record) const;

    bool        readPayload(FILE *f, int payloadLen, QByteArray &payload);
    bool        readRecordHeader(FILE *f, quint16 &recType, quint16 &len);

    bool        readRecord(FILE *f, quint16 &recType, QByteArray &payload);
    QString     decodeGdsString(const QByteArray &payload) const;

private:
    QString     m_fileName;
    mutable QStringList m_errorList;

    FILE       *m_gdsFile = nullptr;

private:
    static constexpr quint16 GDS_HEADER  = 0x0002;
    static constexpr quint16 GDS_BGNLIB  = 0x0102;
    static constexpr quint16 GDS_LIBNAME = 0x0206;
    static constexpr quint16 GDS_UNITS   = 0x0305;
    static constexpr quint16 GDS_ENDLIB  = 0x0400;

    static constexpr quint16 GDS_BGNSTR  = 0x0502;
    static constexpr quint16 GDS_STRNAME = 0x0606;
    static constexpr quint16 GDS_ENDSTR  = 0x0700;

    static constexpr quint16 GDS_SREF    = 0x0A00;
    static constexpr quint16 GDS_AREF    = 0x0B00;
    static constexpr quint16 GDS_SNAME   = 0x1206;
    static constexpr quint16 GDS_ENDEL   = 0x1100;
};

#endif // GDSREADER_H
