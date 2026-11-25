#include "layoutedit.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QXmlStreamReader>

#include <cmath>

#ifndef LIBMAN_NO_ROOM
#include "database.h"
#endif

namespace {

// "Metal1.drawing" → ("Metal1", "drawing"); "Cont" → ("Cont", "").
void splitLayerName(const QString &full, QString *name, QString *purpose)
{
    const int dot = full.lastIndexOf(QLatin1Char('.'));
    if (dot > 0 && dot + 1 < full.size()) {
        *name = full.left(dot);
        *purpose = full.mid(dot + 1);
    } else {
        *name = full;
        purpose->clear();
    }
}

// "8/0", "8/0@1" → layer/datatype; wildcards are not concrete layers.
bool parseLayerSource(const QString &source, int *layer, int *datatype)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d+)\\s*/\\s*(\\d+)"));
    const QRegularExpressionMatch m = re.match(source);
    if (!m.hasMatch()) {
        return false;
    }
    *layer = m.captured(1).toInt();
    *datatype = m.captured(2).toInt();
    return true;
}

} // namespace

QVector<TechLayer> parseKLayoutLyp(const QString &lypPath)
{
    QVector<TechLayer> out;
    QFile file(lypPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return out;
    }

    // <properties> may nest inside <group-members>; keep name/source per open element.
    struct Pending {
        QString name;
        QString source;
    };
    QVector<Pending> stack;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto tag = xml.name();
            if (tag == QLatin1String("properties") || tag == QLatin1String("group-members")) {
                stack.push_back({});
            } else if (!stack.isEmpty() && tag == QLatin1String("name")) {
                stack.last().name = xml.readElementText().trimmed();
            } else if (!stack.isEmpty() && tag == QLatin1String("source")) {
                stack.last().source = xml.readElementText().trimmed();
            }
        } else if (xml.isEndElement()) {
            const auto tag = xml.name();
            if ((tag == QLatin1String("properties") || tag == QLatin1String("group-members"))
                && !stack.isEmpty()) {
                const Pending p = stack.takeLast();
                TechLayer layer;
                if (!p.name.isEmpty() && parseLayerSource(p.source, &layer.layer, &layer.datatype)) {
                    splitLayerName(p.name, &layer.name, &layer.purpose);
                    out.push_back(layer);
                }
            }
        }
    }
    return out;
}

QVector<TechLayer> loadKLayoutTechLayers(const QString &techDir)
{
    QVector<TechLayer> out;
    if (techDir.isEmpty()) {
        return out;
    }
    const QDir dir(techDir);
    for (const QString &name : dir.entryList({QStringLiteral("*.lyp")}, QDir::Files, QDir::Name)) {
        out += parseKLayoutLyp(dir.filePath(name));
    }
    return out;
}

double loadKLayoutTechDbuMicrons(const QString &techDir)
{
    if (techDir.isEmpty()) {
        return 0.0;
    }
    const QDir dir(techDir);
    for (const QString &name : dir.entryList({QStringLiteral("*.lyt")}, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QLatin1String("dbu")) {
                bool ok = false;
                const double dbu = xml.readElementText().trimmed().toDouble(&ok);
                if (ok && dbu > 0.0) {
                    return dbu;
                }
            }
        }
    }
    return 0.0;
}

bool findTechLayer(const QVector<TechLayer> &layers,
                   const QString &query,
                   const QString &purpose,
                   TechLayer *out,
                   QString *error)
{
    const QString q = query.trimmed();
    int wantLayer = -1;
    int wantDatatype = -1;
    if (parseLayerSource(q, &wantLayer, &wantDatatype)
        && q.contains(QRegularExpression(QStringLiteral("^\\s*\\d+\\s*/\\s*\\d+\\s*$")))) {
        for (const TechLayer &layer : layers) {
            if (layer.layer == wantLayer && layer.datatype == wantDatatype) {
                *out = layer;
                return true;
            }
        }
        // Raw numbers are valid even when the tech file does not list them.
        out->name = q;
        out->purpose.clear();
        out->layer = wantLayer;
        out->datatype = wantDatatype;
        return true;
    }

    QString name;
    QString wantPurpose;
    const QStringList words = q.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (words.size() == 2) {
        name = words.at(0);
        wantPurpose = words.at(1);
    } else {
        splitLayerName(q, &name, &wantPurpose);
    }
    if (wantPurpose.isEmpty()) {
        wantPurpose = purpose.trimmed().isEmpty() ? QStringLiteral("drawing") : purpose.trimmed();
    }

    const TechLayer *nameOnly = nullptr;
    int nameMatches = 0;
    for (const TechLayer &layer : layers) {
        if (layer.name.compare(name, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (layer.purpose.compare(wantPurpose, Qt::CaseInsensitive) == 0) {
            *out = layer;
            return true;
        }
        ++nameMatches;
        if (layer.purpose.isEmpty()) {
            nameOnly = &layer;
        }
    }
    if (nameOnly != nullptr && nameMatches == 1) {
        *out = *nameOnly;
        return true;
    }

    if (error != nullptr) {
        QStringList close;
        for (const TechLayer &layer : layers) {
            if (layer.name.contains(name, Qt::CaseInsensitive)) {
                close << QStringLiteral("%1 %2/%3").arg(layer.fullName()).arg(layer.layer).arg(layer.datatype);
                if (close.size() >= 12) {
                    break;
                }
            }
        }
        *error = layers.isEmpty()
            ? QStringLiteral("No technology layers found (PDK layer properties *.lyp missing)")
            : QStringLiteral("Layer '%1' with purpose '%2' not found in technology")
                  .arg(name, wantPurpose);
        if (!close.isEmpty()) {
            *error += QStringLiteral(". Close matches: ") + close.join(QStringLiteral(", "));
        }
    }
    return false;
}

namespace {

QString pythonPurposeName(const QString &purpose)
{
    const QString p = purpose.toLower();
    if (p == QLatin1String("pin")) {
        return QStringLiteral("PIN");
    }
    if (p == QLatin1String("label") || p == QLatin1String("text")) {
        return QStringLiteral("LABEL");
    }
    if (p == QLatin1String("boundary")) {
        return QStringLiteral("BOUNDARY");
    }
    if (p == QLatin1String("block") || p == QLatin1String("blockage")) {
        return QStringLiteral("BLOCKAGE");
    }
    if (p == QLatin1String("filler") || p == QLatin1String("fill")) {
        return QStringLiteral("FILL");
    }
    return QStringLiteral("DRAWING");
}

QString pythonString(const QString &s)
{
    QString t = s;
    t.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    t.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    return QLatin1Char('\'') + t + QLatin1Char('\'');
}

} // namespace

QString layoutRectPythonCode(const QString &roomPath,
                             const QString &cellName,
                             const TechLayer &layer,
                             const LayoutRectEdit &edit)
{
    // Forward slashes on every host: the snippet must stay copy-pasteable from Windows paths.
    const QString path = pythonString(QString(roomPath).replace(QLatin1Char('\\'), QLatin1Char('/')));
    QStringList lines;
    lines << QStringLiteral("import room");
    lines << (edit.createdFile ? QStringLiteral("db = room.create()")
                               : QStringLiteral("db = room.open(%1)").arg(path));
    lines << QStringLiteral("cnt = db.get_or_create_cell(%1).ensure_content('layout', %2)")
                 .arg(pythonString(cellName))
                 .arg(edit.dbuPerMicron);
    lines << QStringLiteral("lid = next((i for i, l in enumerate(db.layers()) "
                            "if (l.layer_num, l.data_type) == (%1, %2)), None)")
                 .arg(layer.layer)
                 .arg(layer.datatype);
    lines << QStringLiteral("if lid is None:");
    lines << QStringLiteral("    lid = db.add_layer(%1, %2, %3, room.LayerPurpose.%4)")
                 .arg(layer.layer)
                 .arg(layer.datatype)
                 .arg(pythonString(layer.fullName()), pythonPurposeName(layer.purpose));
    lines << QStringLiteral("cnt.add_rect(lid, %1, %2, %3, %4)  # dbu, %5 per um")
                 .arg(edit.llx)
                 .arg(edit.lly)
                 .arg(edit.urx)
                 .arg(edit.ury)
                 .arg(edit.dbuPerMicron);
    lines << (edit.createdFile ? QStringLiteral("db.save(%1, 'layout')").arg(path)
                               : QStringLiteral("db.save()"));
    return lines.join(QLatin1Char('\n'));
}

#ifndef LIBMAN_NO_ROOM
namespace {

room::LayerPurpose roomPurpose(const QString &purpose)
{
    const QString p = purpose.toLower();
    if (p == QLatin1String("pin")) {
        return room::LayerPurpose::Pin;
    }
    if (p == QLatin1String("label") || p == QLatin1String("text")) {
        return room::LayerPurpose::Label;
    }
    if (p == QLatin1String("boundary")) {
        return room::LayerPurpose::Boundary;
    }
    if (p == QLatin1String("block") || p == QLatin1String("blockage")) {
        return room::LayerPurpose::Blockage;
    }
    if (p == QLatin1String("filler") || p == QLatin1String("fill")) {
        return room::LayerPurpose::Fill;
    }
    return room::LayerPurpose::Drawing;
}

} // namespace
#endif

bool addRectToLayoutRoom(const QString &roomPath,
                         const QString &cellName,
                         const TechLayer &layer,
                         double xUm, double yUm, double widthUm, double heightUm,
                         double dbuPerMicron,
                         QString *error,
                         LayoutRectEdit *applied)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!(widthUm > 0.0) || !(heightUm > 0.0)) {
        return fail(QStringLiteral("Width and height must be positive"));
    }
    if (cellName.trimmed().isEmpty()) {
        return fail(QStringLiteral("Cell name is empty"));
    }
#ifdef LIBMAN_NO_ROOM
    Q_UNUSED(roomPath);
    Q_UNUSED(layer);
    Q_UNUSED(xUm);
    Q_UNUSED(yUm);
    Q_UNUSED(dbuPerMicron);
    Q_UNUSED(applied);
    return fail(QStringLiteral("Layout editing needs a ROOM build"));
#else
    try {
        room::Database db;
        const bool existed = QFileInfo::exists(roomPath);
        if (existed) {
            db = room::Database::loadFromFile(roomPath.toStdString());
        } else {
            if (!QDir().mkpath(QFileInfo(roomPath).absolutePath())) {
                return fail(QStringLiteral("Cannot create directory for %1").arg(roomPath));
            }
            db.setFileView(room::ViewType::Layout);
        }

        room::Lib &lib = db.lib();
        room::Cell &cell = lib.getOrCreateCell(cellName.toStdString());
        const double newDbu = dbuPerMicron > 0.0 ? dbuPerMicron : 1000.0;
        room::CellContent &content = cell.getOrCreateContent(room::ViewType::Layout, newDbu);
        const double dbu = content.dbuPerMicron() > 0.0 ? content.dbuPerMicron() : newDbu;

        std::uint32_t layerId = 0;
        bool found = false;
        for (std::size_t i = 0; i < lib.layers().size(); ++i) {
            const room::LayerSpec &spec = lib.layers()[i];
            if (spec.layerNum == layer.layer && spec.dataType == layer.datatype) {
                layerId = static_cast<std::uint32_t>(i);
                found = true;
                break;
            }
        }
        if (!found) {
            lib.layers().emplace_back(static_cast<std::uint16_t>(layer.layer),
                                      static_cast<std::uint16_t>(layer.datatype),
                                      layer.fullName().toStdString(),
                                      roomPurpose(layer.purpose));
            layerId = static_cast<std::uint32_t>(lib.layers().size() - 1);
        }

        const auto toDbu = [dbu](double um) { return static_cast<std::int64_t>(std::llround(um * dbu)); };
        room::Shape::RectData rect;
        rect.layerId = layerId;
        rect.box = room::Box(toDbu(xUm), toDbu(yUm), toDbu(xUm + widthUm), toDbu(yUm + heightUm));
        content.block().shapes().emplace_back(std::move(rect));

        lib.recomputeAllBBoxes();
        lib.refreshIndex(room::ViewType::Layout);
        db.saveToFile(roomPath.toStdString(), room::ViewType::Layout);
        if (applied != nullptr) {
            applied->dbuPerMicron = dbu;
            applied->llx = toDbu(xUm);
            applied->lly = toDbu(yUm);
            applied->urx = toDbu(xUm + widthUm);
            applied->ury = toDbu(yUm + heightUm);
            applied->createdFile = !existed;
        }
        return true;
    } catch (const std::exception &e) {
        return fail(QString::fromUtf8(e.what()));
    }
#endif
}
