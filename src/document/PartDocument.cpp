#include "modeling/DimensionAnnotation.h"
#include <BRepTools.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <QCryptographicHash>
#include <sstream>
#include "document/PartDocument.h"
#include "modeling/Box.h"
#include "modeling/ConstraintSolver.h"

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
QJsonObject writePointRef(const modeling::SketchPointRef& ref)
{
    const char* role = nullptr;
    switch (ref.role) {
    case modeling::PointRole::Origin: role = "origin"; break;
    case modeling::PointRole::First: role = "first"; break;
    case modeling::PointRole::Second: role = "second"; break;
    case modeling::PointRole::Center: role = "center"; break;
    default: fail("Unsupported sketch point role.");
    }
    return {{"entity", q(ref.entityId)}, {"point", role}};
}
modeling::SketchPointRef readPointRef(const QJsonValue& value)
{
    if (!value.isObject()) fail("Sketch relation point must be an object.");
    const auto object = value.toObject();
    fields(object, {"entity", "point"}, "Sketch point reference");
    modeling::SketchPointRef ref;
    ref.entityId = stringValue(object.value("entity"), "Point entity ID").toStdString();
    const auto role = stringValue(object.value("point"), "Point role");
    if (role == "origin") ref.role = modeling::PointRole::Origin;
    else if (role == "first") ref.role = modeling::PointRole::First;
    else if (role == "second") ref.role = modeling::PointRole::Second;
    else if (role == "center") ref.role = modeling::PointRole::Center;
    else fail("Unsupported sketch point role.");
    if (ref.role == modeling::PointRole::Origin) {
        if (!ref.entityId.empty()) fail("The fixed sketch origin cannot reference an entity.");
    } else validId(ref.entityId);
    return ref;
}
QJsonObject writeRelation(const modeling::SketchRelation& relation)
{
    const char* type = nullptr;
    switch (relation.kind) {
    case modeling::RelationKind::Coincident: type = "coincident"; break;
    case modeling::RelationKind::Horizontal: type = "horizontal"; break;
    case modeling::RelationKind::Vertical: type = "vertical"; break;
    case modeling::RelationKind::FixedPoint: type = "fixed"; break;
    case modeling::RelationKind::Parallel: type = "parallel"; break;
    case modeling::RelationKind::Perpendicular: type = "perpendicular"; break;
    case modeling::RelationKind::Tangent: type = "tangent"; break;
    default: fail("Unsupported sketch relation type.");
    }
    QJsonObject result{{"id", q(relation.id)}, {"type", type}, {"first", writePointRef(relation.first)}, {"second", writePointRef(relation.second)}};
    if(relation.kind==modeling::RelationKind::FixedPoint)result["position"]=point(relation.fixedPosition);
    return result;
}
modeling::SketchRelation readRelation(const QJsonValue& value, int version)
{
    if (!value.isObject()) fail("Sketch relation must be an object.");
    const auto object = value.toObject();
    QSet<QString> expected{"id", "type", "first", "second"};
    if(version>=5 && object.value("type")=="fixed")expected.insert("position");
    fields(object, expected, "Sketch relation");
    modeling::SketchRelation relation;
    relation.id = stringValue(object.value("id"), "Relation ID").toStdString();
    validId(relation.id);
    const auto type = stringValue(object.value("type"), "Relation type");
    if (type == "coincident") relation.kind = modeling::RelationKind::Coincident;
    else if (type == "horizontal") relation.kind = modeling::RelationKind::Horizontal;
    else if (type == "vertical") relation.kind = modeling::RelationKind::Vertical;
    else if(version>=5 && type=="fixed") {relation.kind=modeling::RelationKind::FixedPoint;relation.fixedPosition=readPoint(object.value("position"));}
    else if(version>=5 && type=="parallel")relation.kind=modeling::RelationKind::Parallel;
    else if(version>=5 && type=="perpendicular")relation.kind=modeling::RelationKind::Perpendicular;
    else if(version>=5 && type=="tangent")relation.kind=modeling::RelationKind::Tangent;
    else fail("Unsupported sketch relation type.");
    relation.first = readPointRef(object.value("first"));
    relation.second = readPointRef(object.value("second"));
    return relation;
}
const char* dimensionName(modeling::DimensionKind kind) {
    switch(kind) {
    case modeling::DimensionKind::LineLength:return "length";
    case modeling::DimensionKind::HorizontalDistance:return "horizontal";
    case modeling::DimensionKind::VerticalDistance:return "vertical";
    case modeling::DimensionKind::CircleRadius:return "radius";
    case modeling::DimensionKind::CircleDiameter:return "diameter";
    case modeling::DimensionKind::PointDistance:return "distance";
    case modeling::DimensionKind::LineDistance:return "line-distance";
    case modeling::DimensionKind::LineAngle:return "angle";
    } fail("Unsupported dimension type.");
}
QJsonObject writeDimension(const modeling::Sketch& sketch,const modeling::SketchDimension& d) {
    return {{"id",q(d.id)},{"type",dimensionName(d.kind)},{"first",writePointRef(d.first)},
        {"second",writePointRef(d.second)},{"value",d.value},{"driving",d.driving},{"annotation",point(modeling::dimensionAnnotationPosition(sketch,d))},{"sense",d.sense},{"placement",d.placement?QJsonValue(point(*d.placement)):QJsonValue(QJsonValue::Null)}};
}
modeling::SketchDimension readDimension(const QJsonValue& value,int version) {
    if(!value.isObject())fail("Dimension must be an object.");
    const auto o=value.toObject();QSet<QString> expected{"id","type","first","second","value","driving","annotation","sense"};
    if(version>=7)expected.insert("placement");fields(o,expected,"Dimension");
    modeling::SketchDimension d;d.id=stringValue(o.value("id"),"Dimension ID").toStdString();validId(d.id);
    const auto type=stringValue(o.value("type"),"Dimension type");bool found=false;
    for(const auto kind:{modeling::DimensionKind::LineLength,modeling::DimensionKind::HorizontalDistance,modeling::DimensionKind::VerticalDistance,modeling::DimensionKind::CircleRadius,modeling::DimensionKind::CircleDiameter,modeling::DimensionKind::PointDistance,modeling::DimensionKind::LineDistance,modeling::DimensionKind::LineAngle})
        if(type==dimensionName(kind) && (version>=6 || int(kind)<=int(modeling::DimensionKind::CircleDiameter))){d.kind=kind;found=true;}
    if(!found)fail("Unsupported dimension type.");
    d.first=readPointRef(o.value("first"));d.second=readPointRef(o.value("second"));d.value=number(o.value("value"),"Dimension value");
    if(!o.value("driving").isBool())fail("Dimension driving status must be boolean.");d.driving=o.value("driving").toBool();
    d.annotation=readPoint(o.value("annotation"));const double sense=number(o.value("sense"),"Dimension sense");
    if(sense!=1 && sense!=-1)fail("Dimension sense must be +1 or -1.");d.sense=int(sense);
    if(version>=7 && !o.value("placement").isNull())d.placement=readPoint(o.value("placement"));return d;
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
std::string bodySignature(const TopoDS_Shape& body) {
    std::ostringstream stream;BRepTools::Write(body,stream,Standard_False,Standard_False,TopTools_FormatVersion_CURRENT);
    return QCryptographicHash::hash(QByteArray::fromStdString(stream.str()),QCryptographicHash::Sha256).toHex().toStdString();
}
gp_Ax3 faceFrame(const TopoDS_Shape& body,int index) {
    TopTools_IndexedMapOfShape faces;TopExp::MapShapes(body,TopAbs_FACE,faces);
    if(index<1 || index>faces.Extent()) fail("The attached face is unavailable. Reselect its support explicitly.");
    return modeling::planarFaceAxes(TopoDS::Face(faces(index)));
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
    modeling::validateSketchRelations(sketch);
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
    std::map<std::string,TopoDS_Shape> bodies;
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
                if(feature.sketch.attachment) {
                    const auto& attachment=*feature.sketch.attachment;
                    const auto source=bodies.find(attachment.sourceFeature);
                    if(source==bodies.end() || bodySignature(source->second)!=attachment.sourceSignature)
                        fail("Sketch face support changed or is missing. Automatic reattachment is disabled; undo the upstream change or recreate the sketch on the intended face.");
                    const auto actual=faceFrame(source->second,attachment.faceIndex);
                    if(actual.Location().Distance(attachment.frame.Location())>1e-7 ||
                       !actual.Direction().IsEqual(attachment.frame.Direction(),1e-9) ||
                       !actual.XDirection().IsEqual(attachment.frame.XDirection(),1e-9)) fail("Stored sketch frame does not match its supporting face.");
                    expected.push_back(attachment.sourceFeature);
                }
                // Legacy equalities must already be internally consistent; dimensional
                // targets and the new solver relations may intentionally require a solve.
                auto legacy=feature.sketch;legacy.dimensions.clear();
                legacy.relations.erase(std::remove_if(legacy.relations.begin(),legacy.relations.end(),[](const auto& r){return r.kind!=modeling::RelationKind::Coincident && r.kind!=modeling::RelationKind::Horizontal && r.kind!=modeling::RelationKind::Vertical;}),legacy.relations.end());
                validSketch(legacy);
                modeling::validateSketchConstraintReferences(feature.sketch);
                modeling::attachDimensionAnnotations(feature.sketch);
                feature.sketch=modeling::requireSolvedSketch(modeling::solvePlanarSketch(feature.sketch));
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
                if(!feature.contours.empty()) {
                    if(!feature.contour.empty())fail("Feature cannot contain both a single contour and multiple regions.");
                    body=feature.kind==FeatureKind::Cut
                        ? modeling::makeCuts(body,source->second,feature.contours,feature.length,feature.throughAll,feature.direction)
                        : modeling::makeExtrusions(source->second,feature.contours,feature.length,feature.direction,body);
                } else if (feature.kind == FeatureKind::Cut) {
                    if (body.IsNull()) fail("Extruded Cut requires an earlier solid.");
                    body = modeling::makeCuts(body,source->second,{feature.contour},feature.length,feature.throughAll,feature.direction);
                } else {
                    body = modeling::makeExtrusions(source->second,{feature.contour},feature.length,feature.direction,body);
                }
                bodyId = feature.id;
            } else fail("Unsupported feature kind.");
            if (validateDependencies && feature.dependencies != expected)
                fail("Invalid dependencies or feature order for " + q(feature.id));
            feature.dependencies = std::move(expected);
            if(feature.kind!=FeatureKind::Sketch) bodies[feature.id]=body;
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

modeling::Sketch PartDocument::sketchOnFace(int index) const {
    if(m_shape.IsNull()) fail("A planar face sketch requires an existing solid.");
    modeling::Sketch sketch;modeling::FaceAttachment attachment;
    for(const auto& feature:m_features) if(feature.kind!=FeatureKind::Sketch) attachment.sourceFeature=feature.id;
    attachment.faceIndex=index;attachment.sourceSignature=bodySignature(m_shape);attachment.frame=faceFrame(m_shape,index);
    sketch.attachment=attachment;return sketch;
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

std::string PartDocument::addExtrudeRegions(const std::string& sketchId,const modeling::RegionReferences& contours,double length,modeling::Direction direction)
{
    if(contours.size()==1)return addExtrude(sketchId,contours.front(),length,direction);
    if(contours.empty())fail("Select at least one closed region.");
    Feature feature;feature.kind=FeatureKind::Extrude;feature.id=nextId(feature.kind);
    feature.name="Extrude"+feature.id.substr(feature.id.find('-')+1);feature.sketchId=sketchId;
    feature.contours=contours;feature.length=length;feature.direction=direction;
    auto candidate=m_features;candidate.push_back(feature);apply(std::move(candidate));return feature.id;
}
std::string PartDocument::addCutRegions(const std::string& sketchId,const modeling::RegionReferences& contours,double depth,bool throughAll,modeling::Direction direction)
{
    if(contours.size()==1)return addCut(sketchId,contours.front(),depth,throughAll,direction);
    if(contours.empty())fail("Select at least one closed region.");
    Feature feature;feature.kind=FeatureKind::Cut;feature.id=nextId(feature.kind);
    feature.name="ExtrudedCut"+feature.id.substr(feature.id.find('-')+1);feature.sketchId=sketchId;
    feature.contours=contours;feature.length=depth;feature.direction=direction;feature.throughAll=throughAll;
    auto candidate=m_features;candidate.push_back(feature);apply(std::move(candidate));return feature.id;
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
    if (!versionValue.isDouble() || (versionValue.toDouble() != 1 && versionValue.toDouble() != 2 && versionValue.toDouble() != 3 && versionValue.toDouble() != 4 && versionValue.toDouble() != 5 && versionValue.toDouble() != 6 && versionValue.toDouble() != 7)) fail("Unsupported part document version; expected versions 1 through 7.");
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
                QSet<QString> expected{"id", "name", "type", "plane", "entities", "dependencies"};
                if (version >= 3) expected.insert("relations");
                if(version>=6){expected.insert("space");expected.insert("dimensions");}
                else if(version>=5)for(const auto* key:{"space","dimensions"})if(object.contains(key))expected.insert(key);
                if(version>=4 && object.contains("attachment")) expected.insert("attachment");
                fields(object, expected, "Sketch feature");
                feature.kind = FeatureKind::Sketch;
                feature.sketch.plane = readPlane(object.value("plane"));
                if(object.contains("space") && object.value("space")!=QJsonValue("planar2d"))fail("Spatial or unsupported sketch space requires a future TheCAD version.");
                if(object.contains("dimensions")){
                    if(!object.value("dimensions").isArray() || object.value("dimensions").toArray().size()>4000)fail("Invalid dimension array (maximum 4000).");
                    for(const auto& d:object.value("dimensions").toArray())feature.sketch.dimensions.push_back(readDimension(d,version));
                }
                if(object.contains("attachment")) {
                    if(!object.value("attachment").isObject()) fail("Invalid face attachment.");
                    const auto a=object.value("attachment").toObject();fields(a,{"source","face","signature","frame"},"Face attachment");
                    modeling::FaceAttachment attachment;attachment.sourceFeature=stringValue(a.value("source"),"Face source").toStdString();
                    const double index=number(a.value("face"),"Face index");if(index<1 || index>100000 || index!=std::floor(index))fail("Invalid face index.");attachment.faceIndex=int(index);
                    attachment.sourceSignature=stringValue(a.value("signature"),"Support signature").toStdString();
                    if(!a.value("frame").isArray() || a.value("frame").toArray().size()!=9)fail("Invalid face frame.");
                    auto frame=a.value("frame").toArray();double v[9];for(int i=0;i<9;++i)v[i]=number(frame[i],"Frame coordinate");
                    gp_Vec n(v[3],v[4],v[5]),x(v[6],v[7],v[8]);
                    if(std::abs(n.Magnitude()-1)>1e-9 || std::abs(x.Magnitude()-1)>1e-9 || std::abs(n.Dot(x))>1e-9)fail("Face frame must be orthonormal.");
                    attachment.frame=gp_Ax3(gp_Pnt(v[0],v[1],v[2]),gp_Dir(n),gp_Dir(x));feature.sketch.attachment=attachment;
                }
                if (!object.value("entities").isArray() || object.value("entities").toArray().size() > int(maximumEntities))
                    fail("Sketch entities must be an array of no more than 1000 elements.");
                for (const auto& entity : object.value("entities").toArray()) feature.sketch.entities.push_back(readEntity(entity));
                if (version >= 3) {
                    if (!object.value("relations").isArray() || object.value("relations").toArray().size() > 4000)
                        fail("Sketch relations must be an array of no more than 4000 elements.");
                    for (const auto& relation : object.value("relations").toArray()) feature.sketch.relations.push_back(readRelation(relation,version));
                }
            } else if (type == "extrude" || type == "cut") {
                QSet<QString> expected{"id", "name", "type", "sketch", "contour", "length", "direction", "dependencies"};
                if (type == "cut") expected.insert("throughAll");
                if(version>=5 && object.contains("contours")){expected.remove("contour");expected.insert("contours");}
                fields(object, expected, "Extrusion feature");
                feature.kind = type == "cut" ? FeatureKind::Cut : FeatureKind::Extrude;
                feature.sketchId = stringValue(object.value("sketch"), "Source sketch").toStdString();
                if(object.contains("contours")) {
                    if(!object.value("contours").isArray() || object.value("contours").toArray().isEmpty() || object.value("contours").toArray().size()>1000)fail("Feature contours must contain 1 to 1000 explicit region reference arrays.");
                    for(const auto& region:object.value("contours").toArray()) {
                        if(!region.isArray() || region.toArray().isEmpty() || region.toArray().size()>1000)fail("Invalid selected region reference array.");
                        std::vector<std::string> ids;for(const auto& id:region.toArray())ids.push_back(stringValue(id,"Region entity ID").toStdString());feature.contours.push_back(std::move(ids));
                    }
                } else {
                    if (!object.value("contour").isArray()) fail("Feature contour must be an explicit array of sketch entity IDs.");
                    for (const auto& entityId : object.value("contour").toArray()) feature.contour.push_back(stringValue(entityId, "Contour entity ID").toStdString());
                }
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
            object["space"]="planar2d";
            QJsonArray dimensions;for(const auto& d:feature.sketch.dimensions)dimensions.append(writeDimension(feature.sketch,d));object["dimensions"]=dimensions;
            if(feature.sketch.attachment) {
                const auto& a=*feature.sketch.attachment;const auto& f=a.frame;const auto p=f.Location();const auto n=f.Direction();const auto x=f.XDirection();
                object["attachment"]=QJsonObject{{"source",q(a.sourceFeature)},{"face",a.faceIndex},{"signature",q(a.sourceSignature)},
                    {"frame",QJsonArray{p.X(),p.Y(),p.Z(),n.X(),n.Y(),n.Z(),x.X(),x.Y(),x.Z()}}};
            }
            QJsonArray entities;
            for (const auto& entity : feature.sketch.entities) entities.append(writeEntity(entity));
            object["entities"] = entities;
            QJsonArray relations;
            for (const auto& relation : feature.sketch.relations) relations.append(writeRelation(relation));
            object["relations"] = relations;
        } else {
            object["sketch"] = q(feature.sketchId);
            QJsonArray contour;
            for (const auto& entityId : feature.contour) contour.append(q(entityId));
            if(feature.contours.empty())object["contour"]=contour;
            else {QJsonArray regions;for(const auto& region:feature.contours){QJsonArray ids;for(const auto& id:region)ids.append(q(id));regions.append(ids);}object["contours"]=regions;}
            object["length"] = feature.length;
            object["direction"] = directionName(feature.direction);
            if (feature.kind == FeatureKind::Cut) object["throughAll"] = feature.throughAll;
        }
        if (feature.kind != FeatureKind::Sketch) resultId = feature.id;
        features.append(object);
    }
    const QJsonObject root{{"format", "TheCAD.Part"}, {"version", 7}, {"units", "mm"}, {"features", features}, {"resultFeature", q(resultId)}};
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (bytes.size() > maximumDocumentBytes) fail("Part document exceeds the 1 MiB prototype size limit.");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) fail("Cannot save part document: " + file.errorString());
    if (file.write(bytes) != bytes.size() || !file.commit()) fail("Cannot save part document atomically: " + file.errorString());
}

} // namespace thecad::document
