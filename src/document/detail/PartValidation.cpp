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

#include "document/detail/PartValidation.h"
namespace thecad::document::detail
{
[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toUtf8().constData());
}
QString q(const std::string& value)
{
    return QString::fromStdString(value);
}
void validId(const std::string& id)
{
    static const QRegularExpression expression(R"(\A[A-Za-z0-9_-]{1,128}\z)");
    if (!expression.match(q(id)).hasMatch())
        fail("Invalid feature identifier.");
}
const char* typeName(FeatureKind kind)
{
    switch (kind)
    {
    case FeatureKind::Box:
        return "box";
    case FeatureKind::Sketch:
        return "sketch";
    case FeatureKind::Extrude:
        return "extrude";
    case FeatureKind::Cut:
        return "cut";
    case FeatureKind::ReferenceGeometry:
        return "reference";
    }
    fail("Unsupported feature kind.");
}
QString planeName(modeling::SketchPlane plane)
{
    switch (plane)
    {
    case modeling::SketchPlane::XY:
        return "XY";
    case modeling::SketchPlane::XZ:
        return "XZ";
    case modeling::SketchPlane::YZ:
        return "YZ";
    }
    fail("Unsupported sketch plane.");
}
QString directionName(modeling::Direction direction)
{
    if (direction == modeling::Direction::Positive)
        return "positive";
    if (direction == modeling::Direction::Negative)
        return "negative";
    fail("Unsupported extrusion direction.");
}
std::string bodySignature(const TopoDS_Shape& body)
{
    return referenceBodySignature(body);
}
gp_Ax3 faceFrame(const TopoDS_Shape& body, int index)
{
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(body, TopAbs_FACE, faces);
    if (index < 1 || index > faces.Extent())
        fail("The attached face is unavailable. Reselect its support explicitly.");
    return modeling::planarFaceAxes(TopoDS::Face(faces(index)));
}
void validSketch(const modeling::Sketch& sketch)
{
    (void)planeName(sketch.plane);
    if (sketch.entities.size() > maximumEntities)
        fail("Sketch exceeds the 1000-entity prototype limit.");
    for (const auto& entity : sketch.entities)
    {
        for (const double coordinate :
             {entity.first.x, entity.first.y, entity.second.x, entity.second.y, entity.radius})
            if (!std::isfinite(coordinate) || std::abs(coordinate) > maximumLength)
                fail("Sketch coordinates must be finite and within the prototype's 1e9 mm limit.");
    }
    modeling::validateSketchRelations(sketch);
    // Open sketches are valid document data. Closed-profile validation belongs
    // to the extrusion operation; drawing geometry still validates entity lengths.
    if (!sketch.entities.empty())
        (void)modeling::sketchShape(sketch);
}
} // namespace thecad::document::detail
