#ifndef LIBFILEPARSER_H
#define LIBFILEPARSER_H

#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>

// Parsed library definition from .lib file
struct LibDefinition
{
    QString                         name;
    QString                         path;
    bool                            hasExplicitName = false;

    QString                         sourceFile;
    int                             sourceLine = -1;

    QString                         technology;
    QStringList                     technologies;

    bool                            hasReplicate = false;
    bool                            replicate = true;

    QMap<QString, QVariant>         options;
};

// Parsed include() statement from .lib file
struct LibInclude
{
    QString                         path;
    QString                         sourceFile;
    int                             sourceLine = -1;
};

// attach(designLibrary, techLibrary) — tech primitive ROOM library name in the same project
struct LibAttach
{
    QString                         libraryName;
    QString                         techLibraryName;
    QString                         sourceFile;
    int                             sourceLine = -1;
};

// pdk(name, path) — Open-PDK / technology root; Door expands via .door-pdk.json (or auto-detect)
struct LibPdk
{
    QString                         name;
    QString                         path;
    QString                         sourceFile;
    int                             sourceLine = -1;
};

// Result of parsing a .lib file tree
struct LibFileData
{
    QList<LibDefinition>            definitions;
    QList<LibInclude>               includes;
    QList<LibAttach>                attaches;
    QList<LibPdk>                   pdks;
};

class LibFileParser
{
public:
    LibFileParser() = default;

    bool                    parseFile(const QString& fileName);
    void                    clear();

    const LibFileData&      data() const;
    QString                 errorString() const;

private:
    struct Statement
    {
        QString             text;
        int                 lineNumber = -1;
    };

private:
    bool                    parseFileInternal(const QString& fileName);
    bool                    parseText(const QString& text, const QString& fileName);

    bool                    parseStatement(const Statement& stmt, const QString& fileName);
    bool                    parseIncludeCall(const QString& argsText,
                          const QString& fileName,
                          int lineNumber);
    bool                    parseDefineCall(const QString& argsText,
                         const QString& fileName,
                         int lineNumber);
    bool                    parseAttachCall(const QString& argsText,
                         const QString& fileName,
                         int lineNumber);
    bool                    parsePdkCall(const QString& argsText,
                      const QString& fileName,
                      int lineNumber);

    QList<Statement>        splitStatements(const QString& text) const;
    QString                 stripComments(const QString& text) const;
    QStringList             splitTopLevel(const QString& text, QChar separator) const;

    bool                    parseStringLiteral(const QString& text, QString& value) const;
    bool                    parseBoolLiteral(const QString& text, bool& value) const;
    bool                    parseStringListLiteral(const QString& text, QStringList& values) const;
    bool                    parseValueLiteral(const QString& text, QVariant& value) const;

    QString                 resolvePathRelativeToFile(const QString& currentFile,
                                      const QString& rawPath) const;
    QString                 inferLibraryNameFromPath(const QString& path) const;
    void                    setError(const QString& fileName, int lineNumber, const QString& message);

private:
    LibFileData             m_data;
    QString                 m_errorString;
    QSet<QString>           m_activeFiles;
    QSet<QString>           m_parsedFiles;
};

#endif // LIBFILEPARSER_H
