#include "modeling/DimensionAnnotation.h"
#include <BRepTools.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <QCryptographicHash>
#include <sstream>
#include "document/PartDocument.h"
#include "document/ReferenceGeometryData.h"
#include "modeling/Box.h"
#include "modeling/ConstraintSolver.h"
#include "modeling/PlanarRegions.h"

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

#include "document/storage/PartFileStore.h"
#include "document/detail/PartValidation.h"
namespace thecad::document
{
using namespace detail;
namespace
{

void fields(const QJsonObject& object, const QSet<QString>& expected, const QString& where)
{
    const auto keys = object.keys();
    if (QSet<QString>(keys.cbegin(), keys.cend()) != expected)
        fail(where +
             ": missing or unsupported fields; this file may require another TheCAD version.");
}
QString stringValue(const QJsonValue& value, const QString& where)
{
    if (!value.isString())
        fail(where + " must be text.");
    return value.toString();
}
double number(const QJsonValue& value, const QString& where)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble()) ||
        std::abs(value.toDouble()) > maximumLength)
        fail(where + " must be a finite number within the prototype's 1e9 mm limit.");
    return value.toDouble();
}

modeling::SketchPlane readPlane(const QJsonValue& value)
{
    const auto text = stringValue(value, "Sketch plane");
    if (text == "XY")
        return modeling::SketchPlane::XY;
    if (text == "XZ")
        return modeling::SketchPlane::XZ;
    if (text == "YZ")
        return modeling::SketchPlane::YZ;
    fail("Unsupported sketch plane; expected XY, XZ, or YZ.");
}

modeling::Direction readDirection(const QJsonValue& value)
{
    const auto text = stringValue(value, "Extrusion direction");
    if (text == "positive")
        return modeling::Direction::Positive;
    if (text == "negative")
        return modeling::Direction::Negative;
    fail("Unsupported extrusion direction; expected positive or negative.");
}
QJsonArray point(modeling::Point2 p)
{
    return {p.x, p.y};
}
modeling::Point2 readPoint(const QJsonValue& value)
{
    if (!value.isArray() || value.toArray().size() != 2)
        fail("Sketch point must contain two coordinates.");
    const auto array = value.toArray();
    return {number(array.at(0), "Sketch X"), number(array.at(1), "Sketch Y")};
}
QJsonObject writeEntity(const modeling::SketchEntity& entity)
{
    QJsonObject object{{"id", q(entity.id)}, {"construction", entity.construction}};
    switch (entity.kind)
    {
    case modeling::EntityKind::Line:
        object["type"] = "line";
        object["first"] = point(entity.first);
        object["second"] = point(entity.second);
        break;
    case modeling::EntityKind::Rectangle:
        object["type"] = "rectangle";
        object["first"] = point(entity.first);
        object["second"] = point(entity.second);
        break;
    case modeling::EntityKind::Circle:
        object["type"] = "circle";
        object["center"] = point(entity.first);
        object["radius"] = entity.radius;
        break;
    case modeling::EntityKind::Point:
        object["type"] = "point";
        object["position"] = point(entity.first);
        break;
    default:
        fail("Unsupported stored entity type.");
    }
    return object;
}
modeling::SketchEntity readEntity(const QJsonValue& value, int version)
{
    if (!value.isObject())
        fail("Sketch entity must be an object.");
    const auto object = value.toObject();
    const auto type = stringValue(object.value("type"), "Entity type");
    modeling::SketchEntity entity;
    entity.id = stringValue(object.value("id"), "Entity ID").toStdString();
    validId(entity.id);
    QSet<QString> expected{"id", "type"};
    if (version >= 8)
    {
        expected.insert("construction");
        if (!object.value("construction").isBool())
            fail("Construction status must be boolean.");
        entity.construction = object.value("construction").toBool();
    }
    if (type == "circle")
    {
        expected.insert("center");
        expected.insert("radius");
        entity.kind = modeling::EntityKind::Circle;
        entity.first = readPoint(object.value("center"));
        entity.radius = number(object.value("radius"), "Radius");
    }
    else if (type == "line" || type == "rectangle")
    {
        expected.insert("first");
        expected.insert("second");
        entity.kind = type == "line" ? modeling::EntityKind::Line : modeling::EntityKind::Rectangle;
        entity.first = readPoint(object.value("first"));
        entity.second = readPoint(object.value("second"));
    }
    else if (type == "point" && version >= 8)
    {
        expected.insert("position");
        entity.kind = modeling::EntityKind::Point;
        entity.first = readPoint(object.value("position"));
    }
    else
        fail("Unsupported entity type.");
    fields(object, expected, "Sketch entity");
    return entity;
}
QJsonObject writePointRef(const modeling::SketchPointRef& ref)
{
    const char* role = nullptr;
    switch (ref.role)
    {
    case modeling::PointRole::Origin:
        role = "origin";
        break;
    case modeling::PointRole::First:
        role = "first";
        break;
    case modeling::PointRole::Second:
        role = "second";
        break;
    case modeling::PointRole::Center:
        role = "center";
        break;
    default:
        fail("Unsupported sketch point role.");
    }
    return {{"entity", q(ref.entityId)}, {"point", role}};
}
modeling::SketchPointRef readPointRef(const QJsonValue& value)
{
    if (!value.isObject())
        fail("Sketch relation point must be an object.");
    const auto object = value.toObject();
    fields(object, {"entity", "point"}, "Sketch point reference");
    modeling::SketchPointRef ref;
    ref.entityId = stringValue(object.value("entity"), "Point entity ID").toStdString();
    const auto role = stringValue(object.value("point"), "Point role");
    if (role == "origin")
        ref.role = modeling::PointRole::Origin;
    else if (role == "first")
        ref.role = modeling::PointRole::First;
    else if (role == "second")
        ref.role = modeling::PointRole::Second;
    else if (role == "center")
        ref.role = modeling::PointRole::Center;
    else
        fail("Unsupported sketch point role.");
    if (ref.role == modeling::PointRole::Origin)
    {
        if (!ref.entityId.empty())
            fail("The fixed sketch origin cannot reference an entity.");
    }
    else
        validId(ref.entityId);
    return ref;
}
QJsonObject writeRelation(const modeling::SketchRelation& relation)
{
    const char* type = nullptr;
    switch (relation.kind)
    {
    case modeling::RelationKind::Collinear:
        type = "collinear";
        break;
    case modeling::RelationKind::Equal:
        type = "equal";
        break;
    case modeling::RelationKind::Concentric:
        type = "concentric";
        break;
    case modeling::RelationKind::EqualCurveLength:
        type = "equal-curve-length";
        break;
    case modeling::RelationKind::FixedEntity:
        type = "fixed-entity";
        break;
    case modeling::RelationKind::Coincident:
        type = "coincident";
        break;
    case modeling::RelationKind::Horizontal:
        type = "horizontal";
        break;
    case modeling::RelationKind::Vertical:
        type = "vertical";
        break;
    case modeling::RelationKind::FixedPoint:
        type = "fixed";
        break;
    case modeling::RelationKind::Parallel:
        type = "parallel";
        break;
    case modeling::RelationKind::Perpendicular:
        type = "perpendicular";
        break;
    case modeling::RelationKind::PointOnLine:
        type = "point-on-line";
        break;
    case modeling::RelationKind::PointOnCircle:
        type = "point-on-circle";
        break;
    case modeling::RelationKind::Midpoint:
        type = "midpoint";
        break;
    case modeling::RelationKind::Tangent:
        type = "tangent";
        break;
    default:
        fail("Unsupported sketch relation type.");
    }
    QJsonObject result{{"id", q(relation.id)},
                       {"type", type},
                       {"first", writePointRef(relation.first)},
                       {"second", writePointRef(relation.second)}};
    if (relation.kind == modeling::RelationKind::FixedPoint)
        result["position"] = point(relation.fixedPosition);
    if (relation.kind == modeling::RelationKind::FixedEntity)
    {
        result["position"] = point(relation.fixedPosition);
        result["secondPosition"] = point(relation.fixedSecond);
        result["radius"] = relation.fixedRadius;
    }
    if (!relation.groupId.empty())
        result["group"] = q(relation.groupId);
    return result;
}
modeling::SketchRelation readRelation(const QJsonValue& value, int version)
{
    if (!value.isObject())
        fail("Sketch relation must be an object.");
    const auto object = value.toObject();
    QSet<QString> expected{"id", "type", "first", "second"};
    if (version >= 10 && object.contains("group"))
        expected.insert("group");
    if (version >= 5 && object.value("type") == "fixed")
        expected.insert("position");
    if (version >= 9 && object.value("type") == "fixed-entity")
    {
        expected.insert("position");
        expected.insert("secondPosition");
        expected.insert("radius");
    }
    fields(object, expected, "Sketch relation");
    modeling::SketchRelation relation;
    relation.id = stringValue(object.value("id"), "Relation ID").toStdString();
    validId(relation.id);
    if (object.contains("group"))
    {
        relation.groupId = stringValue(object.value("group"), "Relation group").toStdString();
        validId(relation.groupId);
    }
    const auto type = stringValue(object.value("type"), "Relation type");
    if (type == "coincident")
        relation.kind = modeling::RelationKind::Coincident;
    else if (type == "horizontal")
        relation.kind = modeling::RelationKind::Horizontal;
    else if (type == "vertical")
        relation.kind = modeling::RelationKind::Vertical;
    else if (version >= 5 && type == "fixed")
    {
        relation.kind = modeling::RelationKind::FixedPoint;
        relation.fixedPosition = readPoint(object.value("position"));
    }
    else if (version >= 5 && type == "parallel")
        relation.kind = modeling::RelationKind::Parallel;
    else if (version >= 5 && type == "perpendicular")
        relation.kind = modeling::RelationKind::Perpendicular;
    else if (version >= 5 && type == "tangent")
        relation.kind = modeling::RelationKind::Tangent;
    else if (version >= 8 && type == "point-on-line")
        relation.kind = modeling::RelationKind::PointOnLine;
    else if (version >= 8 && type == "point-on-circle")
        relation.kind = modeling::RelationKind::PointOnCircle;
    else if (version >= 11 && type == "midpoint")
        relation.kind = modeling::RelationKind::Midpoint;
    else if (version >= 9 && type == "collinear")
        relation.kind = modeling::RelationKind::Collinear;
    else if (version >= 9 && type == "equal")
        relation.kind = modeling::RelationKind::Equal;
    else if (version >= 9 && type == "concentric")
        relation.kind = modeling::RelationKind::Concentric;
    else if (version >= 9 && type == "equal-curve-length")
        relation.kind = modeling::RelationKind::EqualCurveLength;
    else if (version >= 9 && type == "fixed-entity")
    {
        relation.kind = modeling::RelationKind::FixedEntity;
        relation.fixedPosition = readPoint(object.value("position"));
        relation.fixedSecond = readPoint(object.value("secondPosition"));
        relation.fixedRadius = number(object.value("radius"), "Fixed radius");
    }
    else
        fail("Unsupported sketch relation type.");
    relation.first = readPointRef(object.value("first"));
    relation.second = readPointRef(object.value("second"));
    if (version < 9 &&
        (relation.kind == modeling::RelationKind::Horizontal ||
         relation.kind == modeling::RelationKind::Vertical) &&
        relation.first.entityId != relation.second.entityId)
        fail("Multi-point alignment requires schema 9.");
    // The original schema-9 UI identified one Equal batch as UUID-1, UUID-2, ...
    // Recover only that exact generated shape, without merging independent groups.
    if (version == 9 && (relation.kind == modeling::RelationKind::Equal ||
                         relation.kind == modeling::RelationKind::EqualCurveLength))
    {
        static const QRegularExpression batch(
            R"(\A([A-Fa-f0-9]{8}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{12})-[1-9][0-9]*\z)");
        const auto match = batch.match(q(relation.id));
        if (match.hasMatch())
            relation.groupId = match.captured(1).toStdString();
    }
    return relation;
}
const char* dimensionName(modeling::DimensionKind kind)
{
    switch (kind)
    {
    case modeling::DimensionKind::LineLength:
        return "length";
    case modeling::DimensionKind::HorizontalDistance:
        return "horizontal";
    case modeling::DimensionKind::VerticalDistance:
        return "vertical";
    case modeling::DimensionKind::CircleRadius:
        return "radius";
    case modeling::DimensionKind::CircleDiameter:
        return "diameter";
    case modeling::DimensionKind::PointDistance:
        return "distance";
    case modeling::DimensionKind::LineDistance:
        return "line-distance";
    case modeling::DimensionKind::LineAngle:
        return "angle";
    }
    fail("Unsupported dimension type.");
}
QJsonObject writeDimension(const modeling::Sketch& sketch, const modeling::SketchDimension& d)
{
    return {{"id", q(d.id)},
            {"type", dimensionName(d.kind)},
            {"first", writePointRef(d.first)},
            {"second", writePointRef(d.second)},
            {"value", d.value},
            {"driving", d.driving},
            {"annotation", point(modeling::dimensionAnnotationPosition(sketch, d))},
            {"sense", d.sense},
            {"placement",
             d.placement ? QJsonValue(point(*d.placement)) : QJsonValue(QJsonValue::Null)}};
}
modeling::SketchDimension readDimension(const QJsonValue& value, int version)
{
    if (!value.isObject())
        fail("Dimension must be an object.");
    const auto o = value.toObject();
    QSet<QString> expected{"id",    "type",    "first",      "second",
                           "value", "driving", "annotation", "sense"};
    if (version >= 7)
        expected.insert("placement");
    fields(o, expected, "Dimension");
    modeling::SketchDimension d;
    d.id = stringValue(o.value("id"), "Dimension ID").toStdString();
    validId(d.id);
    const auto type = stringValue(o.value("type"), "Dimension type");
    bool found = false;
    for (const auto kind :
         {modeling::DimensionKind::LineLength, modeling::DimensionKind::HorizontalDistance,
          modeling::DimensionKind::VerticalDistance, modeling::DimensionKind::CircleRadius,
          modeling::DimensionKind::CircleDiameter, modeling::DimensionKind::PointDistance,
          modeling::DimensionKind::LineDistance, modeling::DimensionKind::LineAngle})
        if (type == dimensionName(kind) &&
            (version >= 6 || int(kind) <= int(modeling::DimensionKind::CircleDiameter)))
        {
            d.kind = kind;
            found = true;
        }
    if (!found)
        fail("Unsupported dimension type.");
    d.first = readPointRef(o.value("first"));
    d.second = readPointRef(o.value("second"));
    d.value = number(o.value("value"), "Dimension value");
    if (!o.value("driving").isBool())
        fail("Dimension driving status must be boolean.");
    d.driving = o.value("driving").toBool();
    d.annotation = readPoint(o.value("annotation"));
    const double sense = number(o.value("sense"), "Dimension sense");
    if (sense != 1 && sense != -1)
        fail("Dimension sense must be +1 or -1.");
    d.sense = int(sense);
    if (version >= 7 && !o.value("placement").isNull())
        d.placement = readPoint(o.value("placement"));
    return d;
}
QJsonObject writeBox(BoxParameters box)
{
    return {{"width", box.width}, {"depth", box.depth}, {"height", box.height}};
}
BoxParameters readBox(const QJsonValue& value)
{
    if (!value.isObject())
        fail("Box parameters must be an object.");
    const auto object = value.toObject();
    fields(object, {"width", "depth", "height"}, "Box parameters");
    return {number(object.value("width"), "Box width"), number(object.value("depth"), "Box depth"),
            number(object.value("height"), "Box height")};
}

} // namespace
PartDocument PartFileStore::load(const QString& path)
{
    const auto extension = QFileInfo(path).suffix();
    if (extension.compare("thepart", Qt::CaseInsensitive) != 0 &&
        extension.compare("tcpart", Qt::CaseInsensitive) != 0)
        fail("Open a .thepart part or a legacy .tcpart document.");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail("Cannot open part document: " + file.errorString());
    if (file.size() > maximumDocumentBytes)
        fail("Part document exceeds the 1 MiB prototype size limit.");
    const auto bytes = file.read(maximumDocumentBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maximumDocumentBytes)
        fail("Cannot read part document, or it exceeds the size limit.");
    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
        fail("Invalid part document JSON: " + parseError.errorString());
    const auto root = json.object();
    fields(root, {"format", "version", "units", "features", "resultFeature"}, "Part document");
    if (root.value("format") != QJsonValue("TheCAD.Part"))
        fail("Unsupported document format; expected TheCAD.Part.");
    if (root.value("units") != QJsonValue("mm"))
        fail("Unsupported units; this prototype supports millimeters only.");
    const auto versionValue = root.value("version");
    if (!versionValue.isDouble() || versionValue.toDouble() < 1 || versionValue.toDouble() > 16 ||
        versionValue.toDouble() != std::floor(versionValue.toDouble()))
        fail("Unsupported part document version; expected versions 1 through 16.");
    const int version = versionValue.toInt();
    if (!root.value("features").isArray())
        fail("Part features must be an ordered array.");
    const auto array = root.value("features").toArray();
    if (array.size() > int(maximumFeatures) || (version == 1 && array.size() != 1))
        fail("Unsupported feature history size.");
    std::vector<Feature> features;
    for (const auto& value : array)
    {
        if (!value.isObject())
            fail("Feature must be an object.");
        const auto object = value.toObject();
        Feature feature;
        feature.id = stringValue(object.value("id"), "Feature ID").toStdString();
        const auto type = stringValue(object.value("type"), "Feature type");
        if (version == 1)
        {
            fields(object, {"id", "type", "parameters", "dependencies"}, "Legacy box feature");
            if (type != "box")
                fail("Version 1 supports a legacy box only.");
            feature.kind = FeatureKind::Box;
            feature.name = "Box1";
            feature.box = readBox(object.value("parameters"));
        }
        else
        {
            feature.name = stringValue(object.value("name"), "Feature name").toStdString();
            if (type == "box")
            {
                fields(object, {"id", "name", "type", "parameters", "dependencies"}, "Box feature");
                feature.kind = FeatureKind::Box;
                feature.box = readBox(object.value("parameters"));
            }
            else if (type == "reference" && version >= 13)
            {
                QSet<QString> expected{"id", "name", "type", "definition", "dependencies"};
                if (version >= 14)
                {
                    expected.insert("visible");
                    if (!object.value("visible").isBool())
                        fail("Reference visibility must be boolean.");
                    feature.referenceVisible = object.value("visible").toBool();
                }
                fields(object, expected, "Reference geometry feature");
                if (!object.value("definition").isObject())
                    fail("Invalid reference geometry definition.");
                feature.kind = FeatureKind::ReferenceGeometry;
                feature.reference =
                    readDatumDefinition(object.value("definition").toObject(), version);
                if (version < 15)
                    for (const auto& r : feature.reference.references)
                        if (r.source == modeling::ReferenceSource::BodyCircleCenter)
                            fail("Circular-edge center references require schema 15.");
            }
            else if (type == "sketch")
            {
                QSet<QString> expected{"id", "name", "type", "plane", "entities", "dependencies"};
                if (version >= 3)
                    expected.insert("relations");
                if (version >= 8)
                    expected.insert("polygons");
                if (version >= 6)
                {
                    expected.insert("space");
                    expected.insert("dimensions");
                }
                else if (version >= 5)
                    for (const auto* key : {"space", "dimensions"})
                        if (object.contains(key))
                            expected.insert(key);
                if (version >= 4 && object.contains("attachment"))
                    expected.insert("attachment");
                if (version >= 13 && object.contains("referencePlane"))
                    expected.insert("referencePlane");
                fields(object, expected, "Sketch feature");
                feature.kind = FeatureKind::Sketch;
                feature.sketch.plane = readPlane(object.value("plane"));
                if (object.contains("referencePlane"))
                {
                    feature.sketch.referencePlaneId =
                        stringValue(object.value("referencePlane"), "Reference plane support")
                            .toStdString();
                    validId(feature.sketch.referencePlaneId);
                }
                if (object.contains("space") && object.value("space") != QJsonValue("planar2d"))
                    fail("Spatial or unsupported sketch space requires a future TheCAD version.");
                if (object.contains("dimensions"))
                {
                    if (!object.value("dimensions").isArray() ||
                        object.value("dimensions").toArray().size() > 4000)
                        fail("Invalid dimension array (maximum 4000).");
                    for (const auto& d : object.value("dimensions").toArray())
                        feature.sketch.dimensions.push_back(readDimension(d, version));
                }
                if (object.contains("attachment"))
                {
                    if (!object.value("attachment").isObject())
                        fail("Invalid face attachment.");
                    const auto a = object.value("attachment").toObject();
                    auto attachmentFields = QSet<QString>{"source", "face", "signature", "frame"};
                    if (version >= 16)
                        attachmentFields.insert("topology");
                    fields(a, attachmentFields, "Face attachment");
                    modeling::FaceAttachment attachment;
                    attachment.sourceFeature =
                        stringValue(a.value("source"), "Face source").toStdString();
                    const double index = number(a.value("face"), "Face index");
                    if (index < 1 || index > 100000 || index != std::floor(index))
                        fail("Invalid face index.");
                    attachment.faceIndex = int(index);
                    attachment.sourceSignature =
                        stringValue(a.value("signature"), "Support signature").toStdString();
                    if (version >= 16)
                    {
                        if (!a["topology"].isString() || a["topology"].toString().size() > 8192)
                            fail("Invalid support topology name.");
                        attachment.topologyName = a["topology"].toString().toStdString();
                    }
                    if (!a.value("frame").isArray() || a.value("frame").toArray().size() != 9)
                        fail("Invalid face frame.");
                    auto frame = a.value("frame").toArray();
                    double v[9];
                    for (int i = 0; i < 9; ++i)
                        v[i] = number(frame[i], "Frame coordinate");
                    gp_Vec n(v[3], v[4], v[5]), x(v[6], v[7], v[8]);
                    if (std::abs(n.Magnitude() - 1) > 1e-9 || std::abs(x.Magnitude() - 1) > 1e-9 ||
                        std::abs(n.Dot(x)) > 1e-9)
                        fail("Face frame must be orthonormal.");
                    attachment.frame = gp_Ax3(gp_Pnt(v[0], v[1], v[2]), gp_Dir(n), gp_Dir(x));
                    feature.sketch.attachment = attachment;
                }
                if (!object.value("entities").isArray() ||
                    object.value("entities").toArray().size() > int(maximumEntities))
                    fail("Sketch entities must be an array of no more than 1000 elements.");
                for (const auto& entity : object.value("entities").toArray())
                    feature.sketch.entities.push_back(readEntity(entity, version));
                if (version >= 8)
                {
                    if (!object.value("polygons").isArray() ||
                        object.value("polygons").toArray().size() > 1000)
                        fail("Invalid polygon array.");
                    for (const auto& polygonValue : object.value("polygons").toArray())
                    {
                        if (!polygonValue.isObject())
                            fail("Polygon must be an object.");
                        const auto o = polygonValue.toObject();
                        fields(o, {"id", "center", "edges"}, "Polygon");
                        modeling::Sketch::RegularPolygon group;
                        group.id = stringValue(o.value("id"), "Polygon ID").toStdString();
                        validId(group.id);
                        group.centerId =
                            stringValue(o.value("center"), "Polygon center").toStdString();
                        validId(group.centerId);
                        if (!o.value("edges").isArray() || o.value("edges").toArray().size() > 999)
                            fail("Invalid polygon edge array.");
                        for (const auto& id : o.value("edges").toArray())
                        {
                            const auto edge = stringValue(id, "Polygon edge").toStdString();
                            validId(edge);
                            group.edges.push_back(edge);
                        }
                        feature.sketch.polygons.push_back(group);
                    }
                }
                if (version >= 3)
                {
                    if (!object.value("relations").isArray() ||
                        object.value("relations").toArray().size() > 4000)
                        fail("Sketch relations must be an array of no more than 4000 elements.");
                    for (const auto& relation : object.value("relations").toArray())
                        feature.sketch.relations.push_back(readRelation(relation, version));
                }
            }
            else if (type == "extrude" || type == "cut")
            {
                QSet<QString> expected{"id",      "name",   "type",      "sketch",
                                       "contour", "length", "direction", "dependencies"};
                if (type == "cut")
                    expected.insert("throughAll");
                if (version >= 5 && object.contains("contours"))
                {
                    expected.remove("contour");
                    expected.insert("contours");
                }
                fields(object, expected, "Extrusion feature");
                feature.kind = type == "cut" ? FeatureKind::Cut : FeatureKind::Extrude;
                feature.sketchId =
                    stringValue(object.value("sketch"), "Source sketch").toStdString();
                if (object.contains("contours"))
                {
                    if (!object.value("contours").isArray() ||
                        object.value("contours").toArray().isEmpty() ||
                        object.value("contours").toArray().size() > 1000)
                        fail("Feature contours must contain 1 to 1000 explicit region reference "
                             "arrays.");
                    for (const auto& region : object.value("contours").toArray())
                    {
                        if (!region.isArray() || region.toArray().isEmpty() ||
                            region.toArray().size() > 1000)
                            fail("Invalid selected region reference array.");
                        std::vector<std::string> ids;
                        for (const auto& id : region.toArray())
                        {
                            const auto reference =
                                stringValue(id, "Region boundary reference").toStdString();
                            if (version < 12 && modeling::isFragmentReference(reference))
                                fail("Intersection region references require schema 12.");
                            ids.push_back(reference);
                        }
                        feature.contours.push_back(std::move(ids));
                    }
                }
                else
                {
                    if (!object.value("contour").isArray())
                        fail("Feature contour must be an explicit array of sketch entity IDs.");
                    for (const auto& entityId : object.value("contour").toArray())
                    {
                        const auto reference =
                            stringValue(entityId, "Contour boundary reference").toStdString();
                        if (version < 12 && modeling::isFragmentReference(reference))
                            fail("Intersection region references require schema 12.");
                        feature.contour.push_back(reference);
                    }
                }
                feature.length = number(object.value("length"), "Extrusion length");
                feature.direction = readDirection(object.value("direction"));
                if (type == "cut")
                {
                    if (!object.value("throughAll").isBool())
                        fail("Cut throughAll must be boolean.");
                    feature.throughAll = object.value("throughAll").toBool();
                }
            }
            else
                fail("Unsupported feature type: " + type);
        }
        if (!object.value("dependencies").isArray())
            fail("Feature dependencies must be an array.");
        for (const auto& dependency : object.value("dependencies").toArray())
            feature.dependencies.push_back(stringValue(dependency, "Dependency ID").toStdString());
        features.push_back(std::move(feature));
    }
    auto result = PartDocument::createEmpty();
    result.apply(std::move(features), true);
    std::string resultId;
    for (const auto& feature : result.features())
        if (isSolidFeature(feature.kind))
            resultId = feature.id;
    if (root.value("resultFeature") != QJsonValue(q(resultId)))
        fail("resultFeature must identify the final solid-producing feature, or be empty for an "
             "empty/sketch-only part.");
    return result;
}

void PartFileStore::save(const PartDocument& document, const QString& path)
{
    if (QFileInfo(path).suffix().compare("thepart", Qt::CaseInsensitive) != 0)
        fail("Save parts as .thepart. Legacy .tcpart files are read-only inputs and must be "
             "converted to a new path.");
    QJsonArray features;
    std::string resultId;
    for (const auto& feature : document.features())
    {
        QJsonArray dependencies;
        for (const auto& id : feature.dependencies)
            dependencies.append(q(id));
        QJsonObject object{{"id", q(feature.id)},
                           {"name", q(feature.name)},
                           {"type", typeName(feature.kind)},
                           {"dependencies", dependencies}};
        if (feature.kind == FeatureKind::Box)
        {
            object["parameters"] = writeBox(feature.box);
        }
        else if (feature.kind == FeatureKind::ReferenceGeometry)
        {
            object["definition"] = writeDatumDefinition(feature.reference);
            object["visible"] = feature.referenceVisible;
        }
        else if (feature.kind == FeatureKind::Sketch)
        {
            object["plane"] = planeName(feature.sketch.plane);
            object["space"] = "planar2d";
            if (!feature.sketch.referencePlaneId.empty())
                object["referencePlane"] = q(feature.sketch.referencePlaneId);
            QJsonArray polygons;
            for (const auto& g : feature.sketch.polygons)
            {
                QJsonArray edges;
                for (const auto& id : g.edges)
                    edges.append(q(id));
                polygons.append(
                    QJsonObject{{"id", q(g.id)}, {"center", q(g.centerId)}, {"edges", edges}});
            }
            object["polygons"] = polygons;
            QJsonArray dimensions;
            for (const auto& d : feature.sketch.dimensions)
                dimensions.append(writeDimension(feature.sketch, d));
            object["dimensions"] = dimensions;
            if (feature.sketch.attachment)
            {
                const auto& a = *feature.sketch.attachment;
                const auto& f = a.frame;
                const auto p = f.Location();
                const auto n = f.Direction();
                const auto x = f.XDirection();
                object["attachment"] =
                    QJsonObject{{"source", q(a.sourceFeature)},
                                {"face", a.faceIndex},
                                {"signature", q(a.sourceSignature)},
                                {"topology", q(a.topologyName)},
                                {"frame", QJsonArray{p.X(), p.Y(), p.Z(), n.X(), n.Y(), n.Z(),
                                                     x.X(), x.Y(), x.Z()}}};
            }
            QJsonArray entities;
            for (const auto& entity : feature.sketch.entities)
                entities.append(writeEntity(entity));
            object["entities"] = entities;
            QJsonArray relations;
            for (const auto& relation : feature.sketch.relations)
                relations.append(writeRelation(relation));
            object["relations"] = relations;
        }
        else
        {
            object["sketch"] = q(feature.sketchId);
            QJsonArray contour;
            for (const auto& entityId : feature.contour)
                contour.append(q(entityId));
            if (feature.contours.empty())
                object["contour"] = contour;
            else
            {
                QJsonArray regions;
                for (const auto& region : feature.contours)
                {
                    QJsonArray ids;
                    for (const auto& id : region)
                        ids.append(q(id));
                    regions.append(ids);
                }
                object["contours"] = regions;
            }
            object["length"] = feature.length;
            object["direction"] = directionName(feature.direction);
            if (feature.kind == FeatureKind::Cut)
                object["throughAll"] = feature.throughAll;
        }
        if (isSolidFeature(feature.kind))
            resultId = feature.id;
        features.append(object);
    }
    const QJsonObject root{{"format", "TheCAD.Part"},
                           {"version", 16},
                           {"units", "mm"},
                           {"features", features},
                           {"resultFeature", q(resultId)}};
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (bytes.size() > maximumDocumentBytes)
        fail("Part document exceeds the 1 MiB prototype size limit.");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        fail("Cannot save part document: " + file.errorString());
    if (file.write(bytes) != bytes.size() || !file.commit())
        fail("Cannot save part document atomically: " + file.errorString());
}
} // namespace thecad::document
