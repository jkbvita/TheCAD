#include "document/PartDocument.h"
#include "modeling/Box.h"

#include <BRepCheck_Analyzer.hxx>
#include <Standard_Failure.hxx>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace thecad::document {
namespace {
constexpr qint64 maximumDocumentBytes = 1024 * 1024;
constexpr std::size_t maximumFeatures = 500, maximumEntities = 1000;
constexpr double maximumLength = 1e9;
[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toUtf8().constData()); }
QString q(const std::string& value) { return QString::fromStdString(value); }
void fields(const QJsonObject& object, const QSet<QString>& expected, const QString& where)
{
    const auto keys = object.keys();
    if (QSet<QString>(keys.cbegin(), keys.cend()) != expected)
        fail(where + ": missing or unsupported fields; this file may require another TheCAD version.");
}
QString stringValue(const QJsonValue& value, const QString& where)
{
    if (!value.isString()) fail(where + " must be text.");
    return value.toString();
}
double number(const QJsonValue& value, const QString& where)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble()) || std::abs(value.toDouble()) > maximumLength)
        fail(where + " must be a finite number within the prototype's 1e9 mm limit.");
    return value.toDouble();
}
void validId(const std::string& id)
{
    static const QRegularExpression expression(R"(\A[A-Za-z0-9_-]{1,128}\z)");
    if (!expression.match(q(id)).hasMatch()) fail("Invalid feature identifier.");
}
const char* typeName(FeatureKind kind)
{
    switch (kind) {
    case FeatureKind::Box: return "box";
    case FeatureKind::Sketch: return "sketch";
    case FeatureKind::Extrude: return "extrude";
    case FeatureKind::Cut: return "cut";
    }
    fail("Unsupported feature kind.");
}
QString planeName(modeling::SketchPlane plane)
{
    switch (plane) {
    case modeling::SketchPlane::XY: return "XY";
    case modeling::SketchPlane::XZ: return "XZ";
    case modeling::SketchPlane::YZ: return "YZ";
    }
    fail("Unsupported sketch plane.");
}
modeling::SketchPlane readPlane(const QJsonValue& value)
{
    const auto text = stringValue(value, "Sketch plane");
    if (text == "XY") return modeling::SketchPlane::XY;
    if (text == "XZ") return modeling::SketchPlane::XZ;
    if (text == "YZ") return modeling::SketchPlane::YZ;
    fail("Unsupported sketch plane; expected XY, XZ, or YZ.");
}
QString directionName(modeling::Direction direction)
{
    if (direction == modeling::Direction::Positive) return "positive";
    if (direction == modeling::Direction::Negative) return "negative";
    fail("Unsupported extrusion direction.");
}
modeling::Direction readDirection(const QJsonValue& value)
{
    const auto text = stringValue(value, "Extrusion direction");
    if (text == "positive") return modeling::Direction::Positive;
    if (text == "negative") return modeling::Direction::Negative;
    fail("Unsupported extrusion direction; expected positive or negative.");
}
QJsonArray point(modeling::Point2 p) { return {p.x, p.y}; }
modeling::Point2 readPoint(const QJsonValue& value)
{
    if (!value.isArray() || value.toArray().size() != 2) fail("Sketch point must contain two coordinates.");
    const auto array = value.toArray();
    return {number(array.at(0), "Sketch X"), number(array.at(1), "Sketch Y")};
}
QJsonObject writeEntity(const modeling::SketchEntity& entity)
{
    switch (entity.kind) {
    case modeling::EntityKind::Line: return {{"id", q(entity.id)}, {"type", "line"}, {"first", point(entity.first)}, {"second", point(entity.second)}};
    case modeling::EntityKind::Rectangle: return {{"id", q(entity.id)}, {"type", "rectangle"}, {"first", point(entity.first)}, {"second", point(entity.second)}};
    case modeling::EntityKind::Circle: return {{"id", q(entity.id)}, {"type", "circle"}, {"center", point(entity.first)}, {"radius", entity.radius}};
    }
    fail("Unsupported sketch entity type.");
}
modeling::SketchEntity readEntity(const QJsonValue& value)
{
    if (!value.isObject()) fail("Sketch entity must be an object.");
    const auto object = value.toObject();
    const auto type = stringValue(object.value("type"), "Sketch entity type");
    modeling::SketchEntity entity;
    entity.id = stringValue(object.value("id"), "Sketch entity ID").toStdString();
    validId(entity.id);
    if (type == "circle") {
        fields(object, {"id", "type", "center", "radius"}, "Circle");
        entity.kind = modeling::EntityKind::Circle;
        entity.first = readPoint(object.value("center"));
        entity.radius = number(object.value("radius"), "Circle radius");
    } else if (type == "line" || type == "rectangle") {
        fields(object, {"id", "type", "first", "second"}, "Sketch entity");
        entity.kind = type == "line" ? modeling::EntityKind::Line : modeling::EntityKind::Rectangle;
        entity.first = readPoint(object.value("first"));
        entity.second = readPoint(object.value("second"));
    } else fail("Unsupported sketch entity type.");
    return entity;
}
QJsonObject writeBox(BoxParameters box)
{
    return {{"width", box.width}, {"depth", box.depth}, {"height", box.height}};
}
BoxParameters readBox(const QJsonValue& value)
{
    if (!value.isObject()) fail("Box parameters must be an object.");
    const auto object = value.toObject();
    fields(object, {"width", "depth", "height"}, "Box parameters");
    return {number(object.value("width"), "Box width"), number(object.value("depth"), "Box depth"),
            number(object.value("height"), "Box height")};
}
void validSketch(const modeling::Sketch& sketch)
{
    (void)planeName(sketch.plane);
    if (sketch.entities.size() > maximumEntities) fail("Sketch exceeds the 1000-entity prototype limit.");
    for (const auto& entity : sketch.entities) {
        for (const double coordinate : {entity.first.x, entity.first.y, entity.second.x, entity.second.y, entity.radius})
            if (!std::isfinite(coordinate) || std::abs(coordinate) > maximumLength)
                fail("Sketch coordinates must be finite and within the prototype's 1e9 mm limit.");
    }
    // Open sketches are valid document data. Closed-profile validation belongs
    // to the extrusion operation; drawing geometry still validates entity lengths.
    if (!sketch.entities.empty()) (void)modeling::sketchShape(sketch);
}
}

PartDocument PartDocument::createEmpty()
{
    PartDocument result;
    result.m_parameters = {0, 0, 0};
    return result;
}

PartDocument PartDocument::createBox(BoxParameters parameters)
{
    auto result = createEmpty();
    Feature box;
    box.kind = FeatureKind::Box;
    box.id = "box-1";
    box.name = "Box1";
    box.box = parameters;
    result.apply({box});
    return result;
}

void PartDocument::apply(std::vector<Feature> features, bool validateDependencies)
{
    if (features.size() > maximumFeatures) fail("Part exceeds the 500-feature prototype limit.");
    TopoDS_Shape body;
    BoxParameters boxParameters{0, 0, 0};
    std::string bodyId;
    std::map<std::string, modeling::Sketch> sketches;
    std::set<std::string> ids;
    try {
        for (auto& feature : features) {
            validId(feature.id);
            const auto name = q(feature.name);
            if (name.isEmpty() || name.size() > 128 || name.contains(QRegularExpression("[\\x00-\\x1f]")))
                fail("Feature names must contain 1 to 128 printable characters.");
            if (!ids.insert(feature.id).second) fail("Duplicate feature ID: " + q(feature.id));
            std::vector<std::string> expected;
            if (feature.kind == FeatureKind::Box) {
                if (!body.IsNull()) fail("A legacy box cannot replace an existing solid in the history.");
                for (const double length : {feature.box.width, feature.box.depth, feature.box.height})
                    if (!std::isfinite(length) || length > maximumLength) fail("Invalid box dimensions.");
                body = modeling::makeBox(feature.box.width, feature.box.depth, feature.box.height);
                if (!BRepCheck_Analyzer(body).IsValid()) fail("Box parameters do not generate a valid solid.");
                bodyId = feature.id;
                boxParameters = feature.box;
            } else if (feature.kind == FeatureKind::Sketch) {
                std::set<std::string> entityIds;
                for (const auto& entity : feature.sketch.entities) {
                    if (!entity.id.empty()) {
                        validId(entity.id);
                        if (!entityIds.insert(entity.id).second) fail("Duplicate sketch entity ID.");
                    }
                }
                // A new entity must not inherit the identity of an entity removed
                // in this same edit; existing feature contours may reference it.
                // Preserve previous IDs as allocation reservations even when they
                // are absent from the candidate sketch.
                std::set<std::string> reservedIds = entityIds;
                const auto previousSketch = std::find_if(m_features.begin(), m_features.end(),
                    [&](const Feature& old) { return old.kind == FeatureKind::Sketch && old.id == feature.id; });
                if (previousSketch != m_features.end()) {
                    for (const auto& previousEntity : previousSketch->sketch.entities)
                        reservedIds.insert(previousEntity.id);
                }
                std::size_t entityNumber = 1;
                for (auto& entity : feature.sketch.entities) {
                    if (entity.id.empty()) {
                        if (validateDependencies) fail("Stored sketch entities require stable IDs.");
                        std::string candidate;
                        do { candidate = "entity-" + std::to_string(entityNumber++); } while (reservedIds.count(candidate));
                        entity.id = candidate;
                        entityIds.insert(candidate);
                        reservedIds.insert(candidate);
                    }
                }
                validSketch(feature.sketch);
                sketches.emplace(feature.id, feature.sketch);
            } else if (feature.kind == FeatureKind::Extrude || feature.kind == FeatureKind::Cut) {
                const auto source = sketches.find(feature.sketchId);
                if (source == sketches.end()) fail("Feature must reference an earlier sketch: " + q(feature.sketchId));
                (void)directionName(feature.direction);
                if (!std::isfinite(feature.length) || feature.length <= 0 || feature.length > maximumLength)
                    fail("Extrusion length/depth must be positive and no larger than 1e9 mm.");
                expected.push_back(feature.sketchId);
                if (!bodyId.empty()) expected.push_back(bodyId);
                if (feature.kind == FeatureKind::Cut) {
                    if (body.IsNull()) fail("Extruded Cut requires an earlier solid.");
                    body = modeling::makeCut(body, modeling::selectContour(source->second, feature.contour), feature.length, feature.throughAll, feature.direction);
                } else {
                    body = modeling::makeExtrusion(modeling::selectContour(source->second, feature.contour), feature.length, feature.direction, body);
                }
                bodyId = feature.id;
            } else fail("Unsupported feature kind.");
            if (validateDependencies && feature.dependencies != expected)
                fail("Invalid dependencies or feature order for " + q(feature.id));
            feature.dependencies = std::move(expected);
        }
    } catch (const Standard_Failure& error) {
        fail("Cannot regenerate part geometry: " + QString::fromUtf8(error.GetMessageString()));
    } catch (const std::invalid_argument& error) {
        fail(QString::fromUtf8(error.what()));
    }
    m_features = std::move(features);
    m_shape = body;
    m_parameters = boxParameters;
}

std::string PartDocument::nextId(FeatureKind kind) const
{
    const std::string prefix = std::string(typeName(kind)) + "-";
    for (std::size_t number = 1; number <= maximumFeatures + 1; ++number) {
        const std::string candidate = prefix + std::to_string(number);
        if (std::none_of(m_features.begin(), m_features.end(), [&](const Feature& f) { return f.id == candidate; })) return candidate;
    }
    fail("Cannot allocate another feature ID.");
}

std::string PartDocument::addSketch(const modeling::Sketch& sketch)
{
    Feature feature;
    feature.kind = FeatureKind::Sketch;
    feature.id = nextId(feature.kind);
    feature.name = "Sketch" + feature.id.substr(feature.id.find('-') + 1);
    feature.sketch = sketch;
    auto candidate = m_features;
    candidate.push_back(feature);
    apply(std::move(candidate));
    return feature.id;
}

std::string PartDocument::addExtrude(const std::string& sketchId, const std::vector<std::string>& contour, double length, modeling::Direction direction)
{
    Feature feature;
    feature.kind = FeatureKind::Extrude;
    feature.id = nextId(feature.kind);
    feature.name = "Extrude" + feature.id.substr(feature.id.find('-') + 1);
    feature.sketchId = sketchId;
    feature.contour = contour;
    feature.length = length;
    feature.direction = direction;
    auto candidate = m_features;
    candidate.push_back(feature);
    apply(std::move(candidate));
    return feature.id;
}

std::string PartDocument::addCut(const std::string& sketchId, const std::vector<std::string>& contour, double depth, bool throughAll, modeling::Direction direction)
{
    Feature feature;
    feature.kind = FeatureKind::Cut;
    feature.id = nextId(feature.kind);
    feature.name = "ExtrudedCut" + feature.id.substr(feature.id.find('-') + 1);
    feature.sketchId = sketchId;
    feature.contour = contour;
    feature.length = depth;
    feature.throughAll = throughAll;
    feature.direction = direction;
    auto candidate = m_features;
    candidate.push_back(feature);
    apply(std::move(candidate));
    return feature.id;
}

void PartDocument::replaceFeature(const Feature& feature)
{
    auto candidate = m_features;
    const auto current = std::find_if(candidate.begin(), candidate.end(), [&](const Feature& f) { return f.id == feature.id; });
    if (current == candidate.end()) fail("Cannot edit an unknown feature.");
    if (current->kind != feature.kind) fail("Editing cannot change a feature's type.");
    *current = feature;
    apply(std::move(candidate));
}

PartDocument PartDocument::load(const QString& path)
{
    const auto extension = QFileInfo(path).suffix();
    if (extension.compare("thepart", Qt::CaseInsensitive) != 0 && extension.compare("tcpart", Qt::CaseInsensitive) != 0)
        fail("Open a .thepart part or a legacy .tcpart document.");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail("Cannot open part document: " + file.errorString());
    if (file.size() > maximumDocumentBytes) fail("Part document exceeds the 1 MiB prototype size limit.");
    const auto bytes = file.read(maximumDocumentBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maximumDocumentBytes) fail("Cannot read part document, or it exceeds the size limit.");
    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) fail("Invalid part document JSON: " + parseError.errorString());
    const auto root = json.object();
    fields(root, {"format", "version", "units", "features", "resultFeature"}, "Part document");
    if (root.value("format") != QJsonValue("TheCAD.Part")) fail("Unsupported document format; expected TheCAD.Part.");
    if (root.value("units") != QJsonValue("mm")) fail("Unsupported units; this prototype supports millimeters only.");
    const auto versionValue = root.value("version");
    if (!versionValue.isDouble() || (versionValue.toDouble() != 1 && versionValue.toDouble() != 2)) fail("Unsupported part document version; expected 1 or 2.");
    const int version = versionValue.toInt();
    if (!root.value("features").isArray()) fail("Part features must be an ordered array.");
    const auto array = root.value("features").toArray();
    if (array.size() > int(maximumFeatures) || (version == 1 && array.size() != 1)) fail("Unsupported feature history size.");
    std::vector<Feature> features;
    for (const auto& value : array) {
        if (!value.isObject()) fail("Feature must be an object.");
        const auto object = value.toObject();
        Feature feature;
        feature.id = stringValue(object.value("id"), "Feature ID").toStdString();
        const auto type = stringValue(object.value("type"), "Feature type");
        if (version == 1) {
            fields(object, {"id", "type", "parameters", "dependencies"}, "Legacy box feature");
            if (type != "box") fail("Version 1 supports a legacy box only.");
            feature.kind = FeatureKind::Box;
            feature.name = "Box1";
            feature.box = readBox(object.value("parameters"));
        } else {
            feature.name = stringValue(object.value("name"), "Feature name").toStdString();
            if (type == "box") {
                fields(object, {"id", "name", "type", "parameters", "dependencies"}, "Box feature");
                feature.kind = FeatureKind::Box;
                feature.box = readBox(object.value("parameters"));
            } else if (type == "sketch") {
                fields(object, {"id", "name", "type", "plane", "entities", "dependencies"}, "Sketch feature");
                feature.kind = FeatureKind::Sketch;
                feature.sketch.plane = readPlane(object.value("plane"));
                if (!object.value("entities").isArray() || object.value("entities").toArray().size() > int(maximumEntities))
                    fail("Sketch entities must be an array of no more than 1000 elements.");
                for (const auto& entity : object.value("entities").toArray()) feature.sketch.entities.push_back(readEntity(entity));
            } else if (type == "extrude" || type == "cut") {
                QSet<QString> expected{"id", "name", "type", "sketch", "contour", "length", "direction", "dependencies"};
                if (type == "cut") expected.insert("throughAll");
                fields(object, expected, "Extrusion feature");
                feature.kind = type == "cut" ? FeatureKind::Cut : FeatureKind::Extrude;
                feature.sketchId = stringValue(object.value("sketch"), "Source sketch").toStdString();
                if (!object.value("contour").isArray()) fail("Feature contour must be an explicit array of sketch entity IDs.");
                for (const auto& entityId : object.value("contour").toArray()) feature.contour.push_back(stringValue(entityId, "Contour entity ID").toStdString());
                feature.length = number(object.value("length"), "Extrusion length");
                feature.direction = readDirection(object.value("direction"));
                if (type == "cut") {
                    if (!object.value("throughAll").isBool()) fail("Cut throughAll must be boolean.");
                    feature.throughAll = object.value("throughAll").toBool();
                }
            } else fail("Unsupported feature type: " + type);
        }
        if (!object.value("dependencies").isArray()) fail("Feature dependencies must be an array.");
        for (const auto& dependency : object.value("dependencies").toArray())
            feature.dependencies.push_back(stringValue(dependency, "Dependency ID").toStdString());
        features.push_back(std::move(feature));
    }
    auto result = createEmpty();
    result.apply(std::move(features), true);
    std::string resultId;
    for (const auto& feature : result.features()) if (feature.kind != FeatureKind::Sketch) resultId = feature.id;
    if (root.value("resultFeature") != QJsonValue(q(resultId))) fail("resultFeature must identify the final solid-producing feature, or be empty for an empty/sketch-only part.");
    return result;
}

void PartDocument::save(const QString& path) const
{
    if (QFileInfo(path).suffix().compare("thepart", Qt::CaseInsensitive) != 0)
        fail("Save parts as .thepart. Legacy .tcpart files are read-only inputs and must be converted to a new path.");
    QJsonArray features;
    std::string resultId;
    for (const auto& feature : m_features) {
        QJsonArray dependencies;
        for (const auto& id : feature.dependencies) dependencies.append(q(id));
        QJsonObject object{{"id", q(feature.id)}, {"name", q(feature.name)}, {"type", typeName(feature.kind)}, {"dependencies", dependencies}};
        if (feature.kind == FeatureKind::Box) {
            object["parameters"] = writeBox(feature.box);
        } else if (feature.kind == FeatureKind::Sketch) {
            object["plane"] = planeName(feature.sketch.plane);
            QJsonArray entities;
            for (const auto& entity : feature.sketch.entities) entities.append(writeEntity(entity));
            object["entities"] = entities;
        } else {
            object["sketch"] = q(feature.sketchId);
            QJsonArray contour;
            for (const auto& entityId : feature.contour) contour.append(q(entityId));
            object["contour"] = contour;
            object["length"] = feature.length;
            object["direction"] = directionName(feature.direction);
            if (feature.kind == FeatureKind::Cut) object["throughAll"] = feature.throughAll;
        }
        if (feature.kind != FeatureKind::Sketch) resultId = feature.id;
        features.append(object);
    }
    const QJsonObject root{{"format", "TheCAD.Part"}, {"version", 2}, {"units", "mm"}, {"features", features}, {"resultFeature", q(resultId)}};
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (bytes.size() > maximumDocumentBytes) fail("Part document exceeds the 1 MiB prototype size limit.");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) fail("Cannot save part document: " + file.errorString());
    if (file.write(bytes) != bytes.size() || !file.commit()) fail("Cannot save part document atomically: " + file.errorString());
}

} // namespace thecad::document
