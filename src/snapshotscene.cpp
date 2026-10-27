#include "snapshotscene.h"

#include <QDir>
#include <QFileInfo>

#include <cmath>

#ifndef LIBMAN_NO_ROOM
#include "database.h"
#include "em_model_data.h"
#include "enums.h"
#include "room_json_io.h"
#endif

namespace {

#ifndef LIBMAN_NO_ROOM

room::Database loadRoomDb(const QString &path)
{
    room::Database db;
    const room::RoomJsonIo::Result loaded =
        room::RoomJsonIo::loadRoomOrJson(path.toStdString(), db);
    if (!loaded.ok) {
        throw std::runtime_error(loaded.message);
    }
    return db;
}

void includePoint(QRectF *bounds, bool *hasPoint, const QPointF &point)
{
    if (!*hasPoint) {
        *bounds = QRectF(point, QSizeF(0, 0));
        *hasPoint = true;
        return;
    }
    bounds->setLeft(qMin(bounds->left(), point.x()));
    bounds->setRight(qMax(bounds->right(), point.x()));
    bounds->setTop(qMin(bounds->top(), point.y()));
    bounds->setBottom(qMax(bounds->bottom(), point.y()));
}

QString cellNameFromCorePath(const QString &path)
{
    QString name = QFileInfo(path).fileName();
    if (name.endsWith(QStringLiteral(".room"), Qt::CaseInsensitive)) {
        name.chop(5);
    }
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) {
        name = name.left(dot);
    }
    return name;
}

QPointF applyOrient(room::Orient orient, double x, double y)
{
    switch (orient) {
    case room::Orient::R90:
        return QPointF(-y, x);
    case room::Orient::R180:
        return QPointF(-x, -y);
    case room::Orient::R270:
        return QPointF(y, -x);
    case room::Orient::MY:
        return QPointF(-x, y);
    case room::Orient::MX:
        return QPointF(x, -y);
    case room::Orient::MX90:
        return QPointF(y, x);
    case room::Orient::MY90:
        return QPointF(-y, -x);
    case room::Orient::R0:
    default:
        return QPointF(x, y);
    }
}

room::Orient combineOrient(room::Orient outer, room::Orient inner)
{
    const QPointF ix = applyOrient(inner, 1.0, 0.0);
    const QPointF iy = applyOrient(inner, 0.0, 1.0);
    const QPointF x = applyOrient(outer, ix.x(), ix.y());
    const QPointF y = applyOrient(outer, iy.x(), iy.y());
    const auto same = [](const QPointF &point, double px, double py) {
        return std::abs(point.x() - px) < 0.5 && std::abs(point.y() - py) < 0.5;
    };
    if (same(x, 1, 0) && same(y, 0, 1)) {
        return room::Orient::R0;
    }
    if (same(x, 0, 1) && same(y, -1, 0)) {
        return room::Orient::R90;
    }
    if (same(x, -1, 0) && same(y, 0, -1)) {
        return room::Orient::R180;
    }
    if (same(x, 0, -1) && same(y, 1, 0)) {
        return room::Orient::R270;
    }
    if (same(x, -1, 0) && same(y, 0, 1)) {
        return room::Orient::MY;
    }
    if (same(x, 1, 0) && same(y, 0, -1)) {
        return room::Orient::MX;
    }
    if (same(x, 0, 1) && same(y, 1, 0)) {
        return room::Orient::MX90;
    }
    return room::Orient::MY90;
}

room::Transform composeTransform(const room::Transform &outer, const room::Transform &inner)
{
    const double outerMag = outer.mag == 0.0 ? 1.0 : outer.mag;
    const QPointF turned = applyOrient(outer.orient,
                                       static_cast<double>(inner.x) * outerMag,
                                       static_cast<double>(inner.y) * outerMag);
    room::Transform out;
    out.x = static_cast<std::int64_t>(std::llround(turned.x() + static_cast<double>(outer.x)));
    out.y = static_cast<std::int64_t>(std::llround(turned.y() + static_cast<double>(outer.y)));
    out.mag = outerMag * (inner.mag == 0.0 ? 1.0 : inner.mag);
    out.orient = combineOrient(outer.orient, inner.orient);
    return out;
}

std::uint32_t shapeLayerId(const room::Shape &shape)
{
    switch (shape.type()) {
    case room::Shape::Type::Rect:
        return shape.rect() ? shape.rect()->layerId : 0;
    case room::Shape::Type::Polygon:
        return shape.polygon() ? shape.polygon()->layerId : 0;
    case room::Shape::Type::Path:
        return shape.path() ? shape.path()->layerId : 0;
    case room::Shape::Type::Text:
        return shape.text() ? shape.text()->layerId : 0;
    case room::Shape::Type::Arc:
        return shape.arc() ? shape.arc()->layerId : 0;
    }
    return 0;
}

room::LayerPurpose layerPurpose(const room::CellContent &content, std::uint32_t layerId)
{
    const std::vector<room::LayerSpec> &layers = content.layers();
    if (layerId < layers.size()) {
        return layers[layerId].purpose;
    }
    return room::LayerPurpose::Drawing;
}

QColor colorForPurpose(room::LayerPurpose purpose)
{
    switch (purpose) {
    case room::LayerPurpose::Wire:
        return QColor(25, 70, 170);
    case room::LayerPurpose::Pin:
        return QColor(190, 40, 40);
    case room::LayerPurpose::Label:
        return QColor(80, 80, 80);
    default:
        return QColor(25, 25, 25);
    }
}

QColor colorForLayoutLayer(const room::CellContent &content, std::uint32_t layerId)
{
    const room::LayerPurpose purpose = layerPurpose(content, layerId);
    if (purpose == room::LayerPurpose::Pin) {
        return QColor(210, 40, 40, 200);
    }
    std::uint16_t layerNum = static_cast<std::uint16_t>(layerId);
    if (layerId < content.layers().size()) {
        layerNum = content.layers()[layerId].layerNum;
    }
    static const QColor palette[] = {
        QColor(70, 150, 240, 170),
        QColor(40, 170, 80, 170),
        QColor(210, 70, 70, 160),
        QColor(220, 170, 40, 160),
        QColor(150, 70, 190, 160),
        QColor(40, 170, 170, 160),
        QColor(220, 110, 40, 160),
        QColor(120, 120, 130, 150)
    };
    return palette[layerNum % 8];
}

QString propertyValue(const std::vector<room::Property> &properties, const char *key)
{
    for (const room::Property &property : properties) {
        if (property.name == key) {
            return QString::fromStdString(property.value);
        }
    }
    return QString();
}

double editorUnit(const room::CellContent &content)
{
    if (content.dbuPerEditorUnit() > 0.0) {
        return content.dbuPerEditorUnit();
    }
    if (content.dbuPerMicron() > 0.0) {
        return content.dbuPerMicron();
    }
    return 1.0;
}

bool yGrowsUp(const room::CellContent &content)
{
    return QString::fromStdString(content.sourceInfo().format()).compare(QStringLiteral("qucs"),
                                                                          Qt::CaseInsensitive) != 0;
}

class SnapshotBuilder {
public:
    explicit SnapshotBuilder(const SnapshotSymbolResolver &resolver)
        : m_resolver(resolver)
    {
    }

    SnapshotScene scene;

    void drawFile(const QString &path, room::ViewType viewType, const QString &cellName)
    {
        room::Database db = loadRoomDb(path);
        m_lib = &db.lib();
        m_layout = viewType == room::ViewType::Layout;

        const room::Cell *cell = nullptr;
        if (!cellName.isEmpty()) {
            cell = m_lib->findCell(cellName.toStdString());
        }
        if (cell == nullptr) {
            cell = m_lib->findCell(cellNameFromCorePath(path).toStdString());
        }
        if (cell == nullptr && !m_lib->cells().empty()) {
            cell = &m_lib->cells().front();
        }
        if (cell == nullptr) {
            scene.message = QStringLiteral("No cell in this view");
            return;
        }

        const room::CellContent *content = cell->findContent(viewType);
        if (content == nullptr) {
            content = cell->findContent(db.fileView());
        }
        if (content == nullptr) {
            scene.message = QStringLiteral("No geometry in this view");
            return;
        }

        scene.yUp = m_layout || yGrowsUp(*content);
        drawContent(*content, room::Transform(), 1.0, 0);
        if (scene.empty()) {
            scene.message = QStringLiteral("This view has no drawable geometry");
        }
    }

private:
    SnapshotSymbolResolver m_resolver;
    const room::Lib *m_lib = nullptr;
    bool m_layout = false;
    static constexpr int kStrokeBudget = 15000;

    void addStroke(const QPolygonF &points, const QColor &color, bool filled)
    {
        if (points.isEmpty()) {
            return;
        }
        SnapshotStroke stroke;
        stroke.points = points;
        stroke.color = color;
        stroke.filled = filled;
        scene.strokes.push_back(stroke);
        for (const QPointF &point : points) {
            includePoint(&scene.bounds, &m_hasPoint, point);
        }
    }

    void addLabel(const QPointF &pos, const QString &text, const QColor &color)
    {
        const QString trimmed = text.simplified();
        if (trimmed.isEmpty() || trimmed.size() > 48) {
            return;
        }
        SnapshotLabel label;
        label.pos = pos;
        label.text = trimmed;
        label.color = color;
        scene.labels.push_back(label);
        includePoint(&scene.bounds, &m_hasPoint, pos);
    }

    QPointF mapPoint(const room::Transform &transform, double scale, double x, double y) const
    {
        const double mag = transform.mag == 0.0 ? 1.0 : transform.mag;
        const QPointF turned = applyOrient(transform.orient, x * scale * mag, y * scale * mag);
        return QPointF(turned.x() + static_cast<double>(transform.x),
                       turned.y() + static_cast<double>(transform.y));
    }

    void drawContent(const room::CellContent &content,
                     const room::Transform &transform,
                     double scale,
                     int depth)
    {
        const QColor pin = colorForPurpose(room::LayerPurpose::Pin);

        for (const room::Shape &shape : content.block().shapes()) {
            if (scene.strokes.size() >= kStrokeBudget) {
                return;
            }
            const std::uint32_t layerId = shapeLayerId(shape);
            const room::LayerPurpose purpose = layerPurpose(content, layerId);
            const QColor color = m_layout ? colorForLayoutLayer(content, layerId) : colorForPurpose(purpose);
            const bool isPin = purpose == room::LayerPurpose::Pin;
            // Symbols (incl. EM lookalike): outline only, like Qucs/Xschem. Layout keeps fills.
            const bool fillArea = isPin || (m_layout && shape.type() != room::Shape::Type::Path
                                            && shape.type() != room::Shape::Type::Text
                                            && shape.type() != room::Shape::Type::Arc);

            switch (shape.type()) {
            case room::Shape::Type::Path: {
                const room::Shape::PathData *path = shape.path();
                if (path == nullptr || path->points.size() < 2) {
                    break;
                }
                QPolygonF poly;
                for (const room::Point &point : path->points) {
                    poly << mapPoint(transform, scale, static_cast<double>(point.x), static_cast<double>(point.y));
                }
                addStroke(poly, color, false);
                break;
            }
            case room::Shape::Type::Polygon: {
                const room::Shape::PolygonData *polygon = shape.polygon();
                if (polygon == nullptr || polygon->points.empty()) {
                    break;
                }
                QPolygonF poly;
                for (const room::Point &point : polygon->points) {
                    poly << mapPoint(transform, scale, static_cast<double>(point.x), static_cast<double>(point.y));
                }
                addStroke(poly, color, fillArea);
                break;
            }
            case room::Shape::Type::Rect: {
                const room::Shape::RectData *rect = shape.rect();
                if (rect == nullptr) {
                    break;
                }
                const room::Box &box = rect->box;
                QPolygonF poly;
                poly << mapPoint(transform, scale, static_cast<double>(box.llx), static_cast<double>(box.lly));
                poly << mapPoint(transform, scale, static_cast<double>(box.urx), static_cast<double>(box.lly));
                poly << mapPoint(transform, scale, static_cast<double>(box.urx), static_cast<double>(box.ury));
                poly << mapPoint(transform, scale, static_cast<double>(box.llx), static_cast<double>(box.ury));
                if (!isPin && !poly.isEmpty()) {
                    poly << poly.first();
                }
                addStroke(poly, color, fillArea || isPin);
                if (isPin && depth == 0) {
                    const QPointF center = mapPoint(transform,
                                                    scale,
                                                    static_cast<double>(box.llx + box.urx) * 0.5,
                                                    static_cast<double>(box.lly + box.ury) * 0.5);
                    const QString pinName = propertyValue(shape.properties(), "name");
                    addLabel(center, pinName, pin);
                }
                break;
            }
            case room::Shape::Type::Arc: {
                const room::Shape::ArcData *arc = shape.arc();
                if (arc == nullptr || arc->radius <= 0.0) {
                    break;
                }
                double sweep = arc->endAngle - arc->startAngle;
                if (std::abs(sweep) < 1e-6) {
                    sweep = 360.0;
                }
                QPolygonF poly;
                const int steps = 24;
                for (int i = 0; i <= steps; ++i) {
                    const double degrees = arc->startAngle + sweep * (static_cast<double>(i) / steps);
                    const double radians = degrees * 3.14159265358979323846 / 180.0;
                    const double x = static_cast<double>(arc->center.x) + arc->radius * std::cos(radians);
                    const double y = static_cast<double>(arc->center.y) + arc->radius * std::sin(radians);
                    poly << mapPoint(transform, scale, x, y);
                }
                addStroke(poly, color, false);
                break;
            }
            case room::Shape::Type::Text: {
                const room::Shape::TextData *text = shape.text();
                if (text == nullptr || depth > 0 || m_layout) {
                    break;
                }
                addLabel(mapPoint(transform, scale,
                                  static_cast<double>(text->position.x),
                                  static_cast<double>(text->position.y)),
                         QString::fromStdString(text->text),
                         color);
                break;
            }
            }

        }

        if (scene.strokes.size() >= kStrokeBudget) {
            return;
        }
        if (m_layout) {
            if (depth >= 4 || m_lib == nullptr) {
                return;
            }
            for (const room::Instance &instance : content.block().instances()) {
                if (scene.strokes.size() >= kStrokeBudget) {
                    return;
                }
                const room::Cell *child = m_lib->findCell(instance.cellName());
                const room::CellContent *childContent =
                    child == nullptr ? nullptr : child->findContent(room::ViewType::Layout);
                if (childContent == nullptr) {
                    continue;
                }
                drawContent(*childContent, composeTransform(transform, instance.transform()), scale, depth + 1);
            }
            return;
        }
        if (depth > 0) {
            return;
        }

        const double here = editorUnit(content);
        for (const room::Instance &instance : content.block().instances()) {
            const QString cellName = QString::fromStdString(instance.cellName());
            QString symbolPath;
            if (m_resolver) {
                symbolPath = m_resolver(cellName);
            }
            if (!symbolPath.isEmpty()) {
                try {
                    room::Database symbolDb = loadRoomDb(symbolPath);
                    const room::Cell *symbolCell =
                        symbolDb.lib().findCell(cellNameFromCorePath(symbolPath).toStdString());
                    if (symbolCell == nullptr && !symbolDb.lib().cells().empty()) {
                        symbolCell = &symbolDb.lib().cells().front();
                    }
                    const room::CellContent *symbol =
                        symbolCell ? symbolCell->findContent(room::ViewType::Symbol) : nullptr;
                    if (symbol == nullptr && symbolCell != nullptr) {
                        symbol = symbolCell->findContent(symbolDb.fileView());
                    }
                    if (symbol != nullptr) {
                        const double symbolUnit = editorUnit(*symbol);
                        const double symbolScale = symbolUnit > 0.0 ? here / symbolUnit : 1.0;
                        drawContent(*symbol, instance.transform(), symbolScale, depth + 1);
                        continue;
                    }
                } catch (const std::exception &) {
                }
            }

            const double unit = here > 0.0 ? here : 1.0;
            const double halfW = 30.0 * unit;
            const double halfH = 16.0 * unit;
            const double x = static_cast<double>(instance.transform().x);
            const double y = static_cast<double>(instance.transform().y);
            QPolygonF box;
            box << QPointF(x - halfW, y - halfH);
            box << QPointF(x + halfW, y - halfH);
            box << QPointF(x + halfW, y + halfH);
            box << QPointF(x - halfW, y + halfH);
            box << QPointF(x - halfW, y - halfH);
            addStroke(box, QColor(90, 90, 90), false);
            const QString instName = propertyValue(instance.properties(), "name");
            addLabel(QPointF(x, y), instName.isEmpty() ? cellName : instName, QColor(70, 70, 70));
        }
    }

    bool m_hasPoint = false;
};

room::ViewType viewTypeForSnapshot(const QString &viewName)
{
    const QString view = viewName.trimmed().toLower();
    if (view == QStringLiteral("symbol") || view == QStringLiteral("sym")) {
        return room::ViewType::Symbol;
    }
    if (view == QStringLiteral("layout") || view == QStringLiteral("core")) {
        return room::ViewType::Layout;
    }
    return room::ViewType::Schematic;
}

#endif

} // namespace

SnapshotScene loadCoreSnapshot(const QString &corePath,
                               const QString &viewName,
                               const SnapshotSymbolResolver &symbolPathForCell,
                               const QString &cellName)
{
    SnapshotScene scene;
#ifndef LIBMAN_NO_ROOM
    if (corePath.isEmpty() || !QFileInfo::exists(corePath)) {
        scene.message = QStringLiteral("No file for this view");
        return scene;
    }

    // Snapshot draws ROOM (and ROOM JSON). Native .sch/.sym/.gds/… are not ROOM.
    {
        const QString fileName = QFileInfo(corePath).fileName();
        const bool isRoom = fileName.endsWith(QStringLiteral(".room"), Qt::CaseInsensitive);
        const bool isRoomJson = fileName.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)
            && (fileName.contains(QStringLiteral(".schematic."), Qt::CaseInsensitive)
                || fileName.contains(QStringLiteral(".symbol."), Qt::CaseInsensitive)
                || fileName.contains(QStringLiteral(".layout."), Qt::CaseInsensitive)
                || fileName.contains(QStringLiteral(".emmodel."), Qt::CaseInsensitive));
        if (!isRoom && !isRoomJson) {
            scene.message = QStringLiteral("Not a ROOM file");
            return scene;
        }
    }

    try {
        SnapshotBuilder builder(symbolPathForCell);
        builder.drawFile(corePath, viewTypeForSnapshot(viewName), cellName);
        scene = builder.scene;
    } catch (const std::exception &error) {
        const QString what = QString::fromUtf8(error.what());
        // Cap'n Proto noise when a non-ROOM payload was forced through the loader.
        if (what.contains(QStringLiteral("capnp"), Qt::CaseInsensitive)
            || what.contains(QStringLiteral("too many segments"), Qt::CaseInsensitive)
            || what.contains(QStringLiteral("firstWord"), Qt::CaseInsensitive)) {
            scene.message = QStringLiteral("Not a ROOM file");
        } else {
            scene.message = what;
        }
    }
#else
    Q_UNUSED(corePath);
    Q_UNUSED(viewName);
    Q_UNUSED(symbolPathForCell);
    Q_UNUSED(cellName);
    scene.message = QStringLiteral("Snapshot needs a ROOM build");
#endif
    return scene;
}

namespace {

bool looksLikeRoomLayoutPath(const QString &path)
{
    const QString name = QFileInfo(path).fileName().toLower();
    return name.endsWith(QLatin1String(".layout.room"))
        || (name.endsWith(QLatin1String(".room"))
            && !name.endsWith(QLatin1String(".emmodel.room"))
            && !name.endsWith(QLatin1String(".schematic.room"))
            && !name.endsWith(QLatin1String(".symbol.room"))
            && !name.endsWith(QLatin1String(".abstract.room")));
}

QString resolveBeside(const QString &baseFile, const QString &maybeRelative)
{
    const QString trimmed = maybeRelative.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    const QFileInfo fi(trimmed);
    if (fi.isAbsolute()) {
        return QFileInfo(trimmed).absoluteFilePath();
    }
    return QFileInfo(QDir(QFileInfo(baseFile).absolutePath()).filePath(trimmed)).absoluteFilePath();
}

} // namespace

QString layoutRoomPathFromEmModel(const QString &emmodelPath, QString *topCellOut)
{
    if (topCellOut) {
        topCellOut->clear();
    }
#ifndef LIBMAN_NO_ROOM
    if (emmodelPath.isEmpty() || !QFileInfo::exists(emmodelPath)) {
        return {};
    }
    try {
        const room::Database db = loadRoomDb(emmodelPath);
        const room::EmModelViewData *model = nullptr;
        for (const room::Cell &cell : db.lib().cells()) {
            if (const room::CellContent *content = cell.findContent(room::ViewType::EmModel)) {
                if (content->hasEmModelPayload()) {
                    model = &content->emModel();
                    break;
                }
            }
        }
        if (model == nullptr && db.fileView() == room::ViewType::EmModel) {
            for (const room::Cell &cell : db.lib().cells()) {
                if (const room::CellContent *content = cell.findContent(db.fileView())) {
                    if (content->hasEmModelPayload()) {
                        model = &content->emModel();
                        break;
                    }
                }
            }
        }
        if (model == nullptr) {
            return {};
        }

        if (topCellOut && !model->topology.topCell.empty()) {
            *topCellOut = QString::fromStdString(model->topology.topCell);
        }

        const QString layoutPath =
            resolveBeside(emmodelPath, QString::fromStdString(model->topology.layoutPath));
        if (!looksLikeRoomLayoutPath(layoutPath) || !QFileInfo::exists(layoutPath)) {
            return {};
        }
        return QDir::toNativeSeparators(layoutPath);
    } catch (...) {
        return {};
    }
#else
    Q_UNUSED(emmodelPath);
    return {};
#endif
}
