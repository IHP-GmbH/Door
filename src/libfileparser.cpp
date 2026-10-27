#include "libfileparser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

/*!******************************************************************************************************************
 * \brief Clears all internal parser state.
 *
 * Resets previously parsed data, error string and internal bookkeeping structures
 * such as visited and active file sets.
 *******************************************************************************************************************/
void LibFileParser::clear()
{
    m_data.definitions.clear();
    m_data.includes.clear();
    m_data.attaches.clear();
    m_data.pdks.clear();
    m_errorString.clear();
    m_activeFiles.clear();
    m_parsedFiles.clear();
}

/*!******************************************************************************************************************
 * \brief Parses a KLayout .lib file.
 *
 * Starts parsing from the given file and recursively processes all include(...)
 * statements. Existing parser state is cleared before parsing.
 *
 * \param fileName Path to the root .lib file.
 *
 * \return true on success, false on error (see errorString()).
 *******************************************************************************************************************/
bool LibFileParser::parseFile(const QString& fileName)
{
    clear();
    return parseFileInternal(fileName);
}

/*!******************************************************************************************************************
 * \brief Returns parsed library data.
 *
 * Provides access to all parsed define(...) and include(...) statements collected
 * during parsing.
 *
 * \return Constant reference to parsed LibFileData structure.
 *******************************************************************************************************************/
const LibFileData& LibFileParser::data() const
{
    return m_data;
}

/*!******************************************************************************************************************
 * \brief Returns last parser error.
 *
 * The error string contains file name, line number (if available), and a
 * descriptive message explaining the failure reason.
 *
 * \return Error message string, empty if no error occurred.
 *******************************************************************************************************************/
QString LibFileParser::errorString() const
{
    return m_errorString;
}

/*!******************************************************************************************************************
 * \brief Internal recursive parser entry for a single file.
 *
 * Loads file content, detects recursive includes, prevents duplicate parsing,
 * and delegates text parsing to parseText().
 *
 * \param fileName Absolute or relative path to the .lib file.
 *
 * \return true on success, false on error.
 *******************************************************************************************************************/
bool LibFileParser::parseFileInternal(const QString& fileName)
{
    QFileInfo fi(fileName);
    const QString absFileName = fi.absoluteFilePath();

    if(!fi.exists()) {
        setError(absFileName, -1, QString("Lib file does not exist: '%1'.").arg(absFileName));
        return false;
    }

    if(!fi.isFile()) {
        setError(absFileName, -1, QString("Path is not a file: '%1'.").arg(absFileName));
        return false;
    }

    if(m_activeFiles.contains(absFileName)) {
        setError(absFileName, -1, QString("Recursive include detected for '%1'.").arg(absFileName));
        return false;
    }

    if(m_parsedFiles.contains(absFileName)) {
        return true;
    }

    QFile file(absFileName);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(absFileName, -1,
                 QString("Can not read lib file '%1': %2.")
                     .arg(absFileName)
                     .arg(file.errorString()));
        return false;
    }

    QTextStream in(&file);
    const QString text = in.readAll();
    file.close();

    m_activeFiles.insert(absFileName);

    if(!parseText(text, absFileName)) {
        m_activeFiles.remove(absFileName);
        return false;
    }

    m_activeFiles.remove(absFileName);
    m_parsedFiles.insert(absFileName);

    return true;
}

/*!******************************************************************************************************************
 * \brief Parses raw text content of a .lib file.
 *
 * Splits the text into top-level statements and processes each statement
 * individually.
 *
 * \param text Full content of the .lib file.
 * \param fileName Source file name (used for error reporting).
 *
 * \return true on success, false on error.
 *******************************************************************************************************************/
bool LibFileParser::parseText(const QString& text, const QString& fileName)
{
    const QList<Statement> statements = splitStatements(text);

    for(const Statement& stmt : statements) {
        if(!parseStatement(stmt, fileName)) {
            return false;
        }
    }

    return true;
}

/*!******************************************************************************************************************
 * \brief Parses a single top-level statement.
 *
 * Identifies function calls such as include(...) and define(...) and dispatches
 * them to corresponding handlers.
 *
 * \param stmt Statement structure containing text and source line number.
 * \param fileName Source file name.
 *
 * \return true on success, false on error.
 *******************************************************************************************************************/
bool LibFileParser::parseStatement(const Statement& stmt, const QString& fileName)
{
    const QString s = stmt.text.trimmed();
    if(s.isEmpty()) {
        return true;
    }

    const int openPos = s.indexOf('(');
    const int closePos = s.lastIndexOf(')');

    if(openPos <= 0 || closePos < openPos) {
        setError(fileName, stmt.lineNumber,
                 QString("Invalid statement syntax: '%1'.").arg(s));
        return false;
    }

    const QString funcName = s.left(openPos).trimmed();
    const QString argsText = s.mid(openPos + 1, closePos - openPos - 1).trimmed();

    const QString tail = s.mid(closePos + 1).trimmed();
    if(!tail.isEmpty()) {
        setError(fileName, stmt.lineNumber,
                 QString("Unexpected trailing characters in statement: '%1'.").arg(s));
        return false;
    }

    if(funcName == "include") {
        return parseIncludeCall(argsText, fileName, stmt.lineNumber);
    }

    if(funcName == "define") {
        return parseDefineCall(argsText, fileName, stmt.lineNumber);
    }

    if(funcName == "attach") {
        return parseAttachCall(argsText, fileName, stmt.lineNumber);
    }

    if(funcName == "pdk") {
        return parsePdkCall(argsText, fileName, stmt.lineNumber);
    }

    setError(fileName, stmt.lineNumber,
             QString("Unknown function '%1' in statement '%2'.").arg(funcName, s));
    return false;
}

/*!******************************************************************************************************************
 * \brief Parses include(...) function call.
 *
 * Resolves the included file path relative to the current file, stores include
 * information and recursively parses the referenced file.
 *
 * \param argsText Argument list inside include(...).
 * \param fileName Source file name.
 * \param lineNumber Line number of the statement.
 *
 * \return true on success, false on error.
 *******************************************************************************************************************/
bool LibFileParser::parseIncludeCall(const QString& argsText,
                                     const QString& fileName,
                                     int lineNumber)
{
    const QStringList args = splitTopLevel(argsText, ',');
    if(args.size() != 1) {
        setError(fileName, lineNumber,
                 QString("include() expects exactly 1 argument, got %1.").arg(args.size()));
        return false;
    }

    QString includePath;
    if(!parseStringLiteral(args[0].trimmed(), includePath)) {
        setError(fileName, lineNumber,
                 QString("include() argument must be a string literal."));
        return false;
    }

    includePath = resolvePathRelativeToFile(fileName, includePath);

    LibInclude inc;
    inc.path = includePath;
    inc.sourceFile = fileName;
    inc.sourceLine = lineNumber;
    m_data.includes.append(inc);

    return parseFileInternal(includePath);
}

/*!******************************************************************************************************************
 * \brief Parses define(...) function call.
 *
 * Supports both forms:
 *  - define(path)
 *  - define(name, path)
 *
 * Additionally parses optional named arguments such as:
 *  - technology="..."
 *  - technologies=[...]
 *  - replicate=true/false
 *
 * Parsed data is stored in LibDefinition structure.
 *
 * \param argsText Argument list inside define(...).
 * \param fileName Source file name.
 * \param lineNumber Line number of the statement.
 *
 * \return true on success, false on error.
 *******************************************************************************************************************/
bool LibFileParser::parseDefineCall(const QString& argsText,
                                    const QString& fileName,
                                    int lineNumber)
{
    const QStringList args = splitTopLevel(argsText, ',');
    if(args.isEmpty()) {
        setError(fileName, lineNumber,
                 QString("define() expects at least 1 argument."));
        return false;
    }

    QStringList positionalArgs;
    QMap<QString, QString> namedArgs;

    for(const QString& argRaw : args) {
        const QString arg = argRaw.trimmed();
        if(arg.isEmpty()) {
            continue;
        }

        const QStringList eqSplit = splitTopLevel(arg, '=');

        if(eqSplit.size() >= 2) {
            const QString key = eqSplit.first().trimmed();
            const QString value = arg.mid(arg.indexOf('=') + 1).trimmed();

            if(key.isEmpty()) {
                setError(fileName, lineNumber,
                         QString("Empty named option in define()."));
                return false;
            }

            if(namedArgs.contains(key)) {
                setError(fileName, lineNumber,
                         QString("Duplicate named option '%1' in define().").arg(key));
                return false;
            }

            namedArgs.insert(key, value);
        }
        else {
            positionalArgs.append(arg);
        }
    }

    if(positionalArgs.size() != 1 && positionalArgs.size() != 2) {
        setError(fileName, lineNumber,
                 QString("define() expects 1 or 2 positional arguments, got %1.")
                     .arg(positionalArgs.size()));
        return false;
    }

    LibDefinition def;
    def.sourceFile = fileName;
    def.sourceLine = lineNumber;

    if(positionalArgs.size() == 1) {
        QString rawPath;
        if(!parseStringLiteral(positionalArgs[0], rawPath)) {
            setError(fileName, lineNumber,
                     QString("define(path) requires path to be a string literal."));
            return false;
        }

        def.path = resolvePathRelativeToFile(fileName, rawPath);
        def.name = inferLibraryNameFromPath(def.path);
        def.hasExplicitName = false;
    }
    else {
        QString rawName;
        QString rawPath;

        if(!parseStringLiteral(positionalArgs[0], rawName)) {
            setError(fileName, lineNumber,
                     QString("First positional argument of define(name, path) must be a string literal."));
            return false;
        }

        if(!parseStringLiteral(positionalArgs[1], rawPath)) {
            setError(fileName, lineNumber,
                     QString("Second positional argument of define(name, path) must be a string literal."));
            return false;
        }

        def.name = rawName;
        def.path = resolvePathRelativeToFile(fileName, rawPath);
        def.hasExplicitName = true;
    }

    for(QMap<QString, QString>::const_iterator it = namedArgs.constBegin();
         it != namedArgs.constEnd(); ++it) {
        const QString key = it.key();
        const QString valueText = it.value();

        if(key == "technology") {
            QString value;
            if(!parseStringLiteral(valueText, value)) {
                setError(fileName, lineNumber,
                         QString("Option 'technology' must be a string literal."));
                return false;
            }

            def.technology = value;
            def.options.insert(key, value);
        }
        else if(key == "technologies") {
            QStringList values;
            if(!parseStringListLiteral(valueText, values)) {
                setError(fileName, lineNumber,
                         QString("Option 'technologies' must be a string list literal."));
                return false;
            }

            def.technologies = values;
            def.options.insert(key, values);
        }
        else if(key == "replicate") {
            bool value = true;
            if(!parseBoolLiteral(valueText, value)) {
                setError(fileName, lineNumber,
                         QString("Option 'replicate' must be true or false."));
                return false;
            }

            def.hasReplicate = true;
            def.replicate = value;
            def.options.insert(key, value);
        }
        else {
            QVariant value;
            if(parseValueLiteral(valueText, value)) {
                def.options.insert(key, value);
            }
            else {
                def.options.insert(key, valueText);
            }
        }
    }

    m_data.definitions.append(def);
    return true;
}

/*!******************************************************************************************************************
 * \brief Parses attach(designLibrary, techLibrary) function call.
 *******************************************************************************************************************/
bool LibFileParser::parseAttachCall(const QString& argsText,
                                    const QString& fileName,
                                    int lineNumber)
{
    const QStringList args = splitTopLevel(argsText, ',');
    if(args.size() != 2) {
        setError(fileName, lineNumber,
                 QString("attach() expects exactly 2 arguments, got %1.").arg(args.size()));
        return false;
    }

    QString designLib;
    QString techLib;
    if(!parseStringLiteral(args[0].trimmed(), designLib)) {
        setError(fileName, lineNumber,
                 QString("attach() first argument must be a string literal (design library name)."));
        return false;
    }
    if(!parseStringLiteral(args[1].trimmed(), techLib)) {
        setError(fileName, lineNumber,
                 QString("attach() second argument must be a string literal (tech library name)."));
        return false;
    }

    if(designLib.trimmed().isEmpty() || techLib.trimmed().isEmpty()) {
        setError(fileName, lineNumber, QString("attach() library names must not be empty."));
        return false;
    }

    LibAttach attach;
    attach.libraryName = designLib.trimmed();
    attach.techLibraryName = techLib.trimmed();
    attach.sourceFile = fileName;
    attach.sourceLine = lineNumber;
    m_data.attaches.append(attach);
    return true;
}

/*!******************************************************************************************************************
 * \brief Parses pdk(name, path) — technology / Open-PDK root reference.
 *******************************************************************************************************************/
bool LibFileParser::parsePdkCall(const QString& argsText,
                                 const QString& fileName,
                                 int lineNumber)
{
    const QStringList args = splitTopLevel(argsText, ',');
    if(args.size() != 2) {
        setError(fileName, lineNumber,
                 QString("pdk() expects exactly 2 arguments, got %1.").arg(args.size()));
        return false;
    }

    QString name;
    QString rawPath;
    if(!parseStringLiteral(args[0].trimmed(), name)) {
        setError(fileName, lineNumber,
                 QString("pdk() first argument must be a string literal (PDK name)."));
        return false;
    }
    if(!parseStringLiteral(args[1].trimmed(), rawPath)) {
        setError(fileName, lineNumber,
                 QString("pdk() second argument must be a string literal (PDK path)."));
        return false;
    }

    name = name.trimmed();
    if(name.isEmpty()) {
        setError(fileName, lineNumber, QString("pdk() name must not be empty."));
        return false;
    }

    LibPdk pdk;
    pdk.name = name;
    pdk.path = resolvePathRelativeToFile(fileName, rawPath);
    pdk.sourceFile = fileName;
    pdk.sourceLine = lineNumber;
    m_data.pdks.append(pdk);
    return true;
}

/*!******************************************************************************************************************
 * \brief Splits file text into top-level statements.
 *
 * Statements are separated by ';'. The splitter respects string literals,
 * parentheses '()' and list brackets '[]' to avoid incorrect splitting inside
 * nested structures.
 *
 * Also tracks line numbers for each statement.
 *
 * \param text Input text.
 *
 * \return List of parsed statements with corresponding line numbers.
 *******************************************************************************************************************/
QList<LibFileParser::Statement> LibFileParser::splitStatements(const QString& text) const
{
    QList<Statement> result;

    QString current;
    bool inString = false;
    bool escape = false;
    int parenDepth = 0;
    int bracketDepth = 0;
    int lineNumber = 1;
    int stmtStartLine = 1;

    const QString cleaned = stripComments(text);

    for(int i = 0; i < cleaned.size(); ++i) {
        const QChar ch = cleaned.at(i);

        if(current.isEmpty() && !ch.isSpace()) {
            stmtStartLine = lineNumber;
        }

        if(inString) {
            current += ch;

            if(escape) {
                escape = false;
            }
            else if(ch == '\\') {
                escape = true;
            }
            else if(ch == '"') {
                inString = false;
            }
        }
        else {
            if(ch == '"') {
                inString = true;
                current += ch;
            }
            else if(ch == '(') {
                ++parenDepth;
                current += ch;
            }
            else if(ch == ')') {
                --parenDepth;
                current += ch;
            }
            else if(ch == '[') {
                ++bracketDepth;
                current += ch;
            }
            else if(ch == ']') {
                --bracketDepth;
                current += ch;
            }
            else if(ch == ';' && parenDepth == 0 && bracketDepth == 0) {
                const QString stmtText = current.trimmed();
                if(!stmtText.isEmpty()) {
                    Statement stmt;
                    stmt.text = stmtText;
                    stmt.lineNumber = stmtStartLine;
                    result.append(stmt);
                }
                current.clear();
            }
            else {
                current += ch;
            }
        }

        if(ch == '\n') {
            ++lineNumber;
        }
    }

    const QString tail = current.trimmed();
    if(!tail.isEmpty()) {
        Statement stmt;
        stmt.text = tail;
        stmt.lineNumber = stmtStartLine;
        result.append(stmt);
    }

    return result;
}

/*!******************************************************************************************************************
 * \brief Removes line comments from text.
 *
 * Strips comments starting with '#' until end-of-line, while preserving content
 * inside string literals.
 *
 * \param text Input text.
 *
 * \return Text with comments removed.
 *******************************************************************************************************************/
QString LibFileParser::stripComments(const QString& text) const
{
    QString result;
    bool inString = false;
    bool escape = false;
    bool inComment = false;

    for(int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);

        if(inComment) {
            if(ch == '\n') {
                inComment = false;
                result += ch;
            }
            continue;
        }

        if(inString) {
            result += ch;

            if(escape) {
                escape = false;
            }
            else if(ch == '\\') {
                escape = true;
            }
            else if(ch == '"') {
                inString = false;
            }

            continue;
        }

        if(ch == '"') {
            inString = true;
            result += ch;
            continue;
        }

        if(ch == '#') {
            inComment = true;
            continue;
        }

        result += ch;
    }

    return result;
}

/*!******************************************************************************************************************
 * \brief Splits text by a separator at top level.
 *
 * Splits the string using the given separator character while ignoring separators
 * inside string literals, parentheses '()' and list brackets '[]'.
 *
 * Used for splitting argument lists and key-value pairs.
 *
 * \param text Input text.
 * \param separator Separator character.
 *
 * \return List of split elements.
 *******************************************************************************************************************/
QStringList LibFileParser::splitTopLevel(const QString& text, QChar separator) const
{
    QStringList result;
    QString current;

    bool inString = false;
    bool escape = false;
    int parenDepth = 0;
    int bracketDepth = 0;

    for(int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);

        if(inString) {
            current += ch;

            if(escape) {
                escape = false;
            }
            else if(ch == '\\') {
                escape = true;
            }
            else if(ch == '"') {
                inString = false;
            }

            continue;
        }

        if(ch == '"') {
            inString = true;
            current += ch;
            continue;
        }

        if(ch == '(') {
            ++parenDepth;
            current += ch;
            continue;
        }

        if(ch == ')') {
            --parenDepth;
            current += ch;
            continue;
        }

        if(ch == '[') {
            ++bracketDepth;
            current += ch;
            continue;
        }

        if(ch == ']') {
            --bracketDepth;
            current += ch;
            continue;
        }

        if(ch == separator && parenDepth == 0 && bracketDepth == 0) {
            result.append(current.trimmed());
            current.clear();
            continue;
        }

        current += ch;
    }

    result.append(current.trimmed());
    return result;
}

/*!******************************************************************************************************************
 * \brief Parses a string literal.
 *
 * Accepts strings enclosed in double quotes and supports escape sequences such as
 * \n, \t, \r, \\, and \".
 *
 * \param text Input text.
 * \param value Output parsed string.
 *
 * \return true if parsing succeeded, false otherwise.
 *******************************************************************************************************************/
bool LibFileParser::parseStringLiteral(const QString& text, QString& value) const
{
    const QString s = text.trimmed();

    if(s.size() < 2) {
        return false;
    }

    if(!s.startsWith('"') || !s.endsWith('"')) {
        return false;
    }

    QString out;
    bool escape = false;

    for(int i = 1; i < s.size() - 1; ++i) {
        const QChar ch = s.at(i);

        if(escape) {
            switch(ch.toLatin1()) {
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case '\\':
                out += '\\';
                break;
            case '"':
                out += '"';
                break;
            default:
                out += ch;
                break;
            }
            escape = false;
        }
        else if(ch == '\\') {
            escape = true;
        }
        else {
            out += ch;
        }
    }

    if(escape) {
        return false;
    }

    value = out;
    return true;
}

/*!******************************************************************************************************************
 * \brief Parses a boolean literal.
 *
 * Accepts "true" or "false".
 *
 * \param text Input text.
 * \param value Output boolean value.
 *
 * \return true if parsing succeeded, false otherwise.
 *******************************************************************************************************************/
bool LibFileParser::parseBoolLiteral(const QString& text, bool& value) const
{
    const QString s = text.trimmed();

    if(s == "true") {
        value = true;
        return true;
    }

    if(s == "false") {
        value = false;
        return true;
    }

    return false;
}

/*!******************************************************************************************************************
 * \brief Parses a list of string literals.
 *
 * Accepts syntax of the form ["A","B",...].
 *
 * \param text Input text.
 * \param values Output list of parsed strings.
 *
 * \return true if parsing succeeded, false otherwise.
 *******************************************************************************************************************/
bool LibFileParser::parseStringListLiteral(const QString& text, QStringList& values) const
{
    values.clear();

    const QString s = text.trimmed();
    if(s.size() < 2 || !s.startsWith('[') || !s.endsWith(']')) {
        return false;
    }

    const QString inner = s.mid(1, s.size() - 2).trimmed();
    if(inner.isEmpty()) {
        return true;
    }

    const QStringList elems = splitTopLevel(inner, ',');
    for(const QString& elemRaw : elems) {
        QString elem;
        if(!parseStringLiteral(elemRaw.trimmed(), elem)) {
            return false;
        }
        values.append(elem);
    }

    return true;
}

/*!******************************************************************************************************************
 * \brief Parses a generic literal value.
 *
 * Attempts to parse the input as:
 *  - string literal
 *  - boolean literal
 *  - list of strings
 *
 * \param text Input text.
 * \param value Output QVariant containing parsed value.
 *
 * \return true if recognized and parsed, false otherwise.
 *******************************************************************************************************************/
bool LibFileParser::parseValueLiteral(const QString& text, QVariant& value) const
{
    QString s;
    if(parseStringLiteral(text, s)) {
        value = s;
        return true;
    }

    bool b = false;
    if(parseBoolLiteral(text, b)) {
        value = b;
        return true;
    }

    QStringList list;
    if(parseStringListLiteral(text, list)) {
        value = list;
        return true;
    }

    return false;
}

/*!******************************************************************************************************************
 * \brief Resolves absolute or relative path with respect to current lib file.
 *
 * Relative paths are interpreted relative to the directory of the file where the
 * statement is defined.
 *
 * \param currentFile Path to the current .lib file.
 * \param rawPath Raw path string from include(...) or define(...).
 *
 * \return Absolute normalized path.
 *******************************************************************************************************************/
QString LibFileParser::resolvePathRelativeToFile(const QString& currentFile,
                                                 const QString& rawPath) const
{
    QString p = rawPath;
    p.replace('\\', '/');

    // Keep wildcard define patterns intact (e.g. analogLib/*). QFileInfo would drop the suffix.
    if(p.contains(QLatin1Char('*'))) {
        if(QDir::isAbsolutePath(p)) {
            return QDir::toNativeSeparators(QDir::cleanPath(p));
        }
        return QDir::toNativeSeparators(p);
    }

    if(QDir::isAbsolutePath(p)) {
        return QDir::toNativeSeparators(QFileInfo(p).absoluteFilePath());
    }

    const QDir baseDir = QFileInfo(currentFile).absoluteDir();
    return QDir::toNativeSeparators(QFileInfo(baseDir.absoluteFilePath(p)).absoluteFilePath());
}

/*!******************************************************************************************************************
 * \brief Infers library name from file path.
 *
 * Extracts the base name (without extension) from the given path and uses it as
 * the library name when define(path) form is used.
 *
 * \param path Library file path.
 *
 * \return Inferred library name.
 *******************************************************************************************************************/
QString LibFileParser::inferLibraryNameFromPath(const QString& path) const
{
    return QFileInfo(path).completeBaseName();
}

/*!******************************************************************************************************************
 * \brief Sets parser error message.
 *
 * Formats error string including file name and optional line number.
 *
 * \param fileName Source file name.
 * \param lineNumber Line number (or -1 if not applicable).
 * \param message Error description.
 *******************************************************************************************************************/
void LibFileParser::setError(const QString& fileName, int lineNumber, const QString& message)
{
    if(lineNumber > 0) {
        m_errorString = QString("%1:%2: %3").arg(fileName).arg(lineNumber).arg(message);
    }
    else {
        m_errorString = QString("%1: %2").arg(fileName, message);
    }
}
