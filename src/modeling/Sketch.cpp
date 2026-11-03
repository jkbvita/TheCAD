#include "modeling/Sketch.h"

#include <BRep_Builder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopTools_ListOfShape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <set>
#include <map>
#include <numeric>
#include <tuple>
#include <utility>
#include <vector>

namespace thecad::modeling {
namespace {
constexpr double coordinateLimit = 1e9;
constexpr double joiningTolerance = 1e-6;
constexpr std::size_t maximumEntities = 1000;

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

void finiteCoordinate(double value)
{
    if (!std::isfinite(value) || std::abs(value) > coordinateLimit) {
        fail("Sketch coordinates must be finite and within +/-1,000,000,000 mm.");
    }
}

void positiveLength(double value, const char* name)
{
    if (!std::isfinite(value) || value <= Precision::Confusion() || value > coordinateLimit) {
        fail(std::string(name) + " must be finite, above the kernel tolerance, and no larger than 1,000,000,000 mm.");
    }
}

double distance(Point2 first, Point2 second)
{
    return std::hypot(first.x - second.x, first.y - second.y);
}

bool samePoint(Point2 first, Point2 second)
{
    return distance(first, second) <= joiningTolerance;
}

void validate(const Sketch& sketch)
{
    (void)sketchAxes(sketch.plane);
    validateSketchRelations(sketch);
    if (sketch.entities.size() > maximumEntities) {
        fail("This prototype supports at most 1000 entities per sketch.");
    }
    for (const auto& entity : sketch.entities) {
        finiteCoordinate(entity.first.x);
        finiteCoordinate(entity.first.y);
        finiteCoordinate(entity.second.x);
        finiteCoordinate(entity.second.y);
        switch (entity.kind) {
        case EntityKind::Line:
            if (samePoint(entity.first, entity.second)) {
                fail("A line needs two distinct points.");
            }
            break;
        case EntityKind::Rectangle:
            if (std::abs(entity.first.x - entity.second.x) <= joiningTolerance
                || std::abs(entity.first.y - entity.second.y) <= joiningTolerance) {
                fail("A rectangle must have nonzero width and height.");
            }
            break;
        case EntityKind::Circle:
            positiveLength(entity.radius, "Circle radius");
            break;
        default:
            fail("Unsupported sketch entity type.");
        }
    }
}

std::vector<Point2> rectanglePoints(const SketchEntity& entity)
{
    const double xmin = std::min(entity.first.x, entity.second.x);
    const double xmax = std::max(entity.first.x, entity.second.x);
    const double ymin = std::min(entity.first.y, entity.second.y);
    const double ymax = std::max(entity.first.y, entity.second.y);
    return {{xmin, ymin}, {xmax, ymin}, {xmax, ymax}, {xmin, ymax}};
}

TopoDS_Edge lineEdge(SketchPlane plane, Point2 first, Point2 second)
{
    BRepBuilderAPI_MakeEdge builder(sketchPoint(plane, first), sketchPoint(plane, second));
    if (!builder.IsDone()) {
        fail("The line could not be constructed by the geometry kernel.");
    }
    return builder.Edge();
}

TopoDS_Edge circleEdge(SketchPlane plane, const SketchEntity& entity)
{
    const auto axes = sketchAxes(plane);
    const gp_Ax2 circleAxes(sketchPoint(plane, entity.first), axes.Direction(), axes.XDirection());
    BRepBuilderAPI_MakeEdge builder(gp_Circ(circleAxes, entity.radius));
    if (!builder.IsDone()) {
        fail("The circle could not be constructed by the geometry kernel.");
    }
    return builder.Edge();
}

double cross(Point2 first, Point2 second, Point2 third)
{
    return (second.x - first.x) * (third.y - first.y)
         - (second.y - first.y) * (third.x - first.x);
}

int side(Point2 first, Point2 second, Point2 point)
{
    const double value = cross(first, second, point);
    const double threshold = joiningTolerance * std::max(distance(first, second), 1.0);
    return value > threshold ? 1 : value < -threshold ? -1 : 0;
}

bool onSegment(Point2 first, Point2 second, Point2 point)
{
    return side(first, second, point) == 0
        && point.x >= std::min(first.x, second.x) - joiningTolerance
        && point.x <= std::max(first.x, second.x) + joiningTolerance
        && point.y >= std::min(first.y, second.y) - joiningTolerance
        && point.y <= std::max(first.y, second.y) + joiningTolerance;
}

bool intersects(Point2 a, Point2 b, Point2 c, Point2 d)
{
    const int abC = side(a, b, c), abD = side(a, b, d);
    const int cdA = side(c, d, a), cdB = side(c, d, b);
    return (abC * abD < 0 && cdA * cdB < 0)
        || (abC == 0 && onSegment(a, b, c))
        || (abD == 0 && onSegment(a, b, d))
        || (cdA == 0 && onSegment(c, d, a))
        || (cdB == 0 && onSegment(c, d, b));
}

std::vector<Point2> orderedLineLoop(const Sketch& sketch)
{
    if (sketch.entities.size() < 3) {
        fail("A closed line profile needs at least three connected lines.");
    }
    std::vector<Point2> nodes;
    std::vector<std::vector<int>> incident;
    std::vector<std::pair<int, int>> edges;
    const auto nodeFor = [&nodes, &incident](Point2 point) {
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            if (samePoint(nodes[index], point)) {
                return static_cast<int>(index);
            }
        }
        nodes.push_back(point);
        incident.emplace_back();
        return static_cast<int>(nodes.size() - 1);
    };
    for (const auto& entity : sketch.entities) {
        if (entity.kind != EntityKind::Line) {
            fail("Use one rectangle, one circle, or one connected loop of lines. Mixed or multiple profiles are not supported yet.");
        }
        const int first = nodeFor(entity.first);
        const int second = nodeFor(entity.second);
        const int edgeIndex = static_cast<int>(edges.size());
        edges.emplace_back(first, second);
        incident[first].push_back(edgeIndex);
        incident[second].push_back(edgeIndex);
    }
    for (const auto& links : incident) {
        if (links.size() != 2) {
            fail("The profile is open or branched. Every line endpoint must join exactly two lines.");
        }
    }
    std::vector<Point2> polygon;
    std::vector<bool> visited(edges.size(), false);
    int node = 0;
    for (std::size_t step = 0; step < edges.size(); ++step) {
        polygon.push_back(nodes[node]);
        const auto& links = incident[node];
        const int edge = !visited[links[0]] ? links[0] : links[1];
        if (visited[edge]) {
            fail("The sketch contains disconnected profiles. Select one simple closed loop.");
        }
        visited[edge] = true;
        node = edges[edge].first == node ? edges[edge].second : edges[edge].first;
        if (node == 0 && step + 1 < edges.size()) {
            fail("The sketch contains multiple closed loops. This prototype extrudes one profile at a time.");
        }
    }
    if (node != 0) {
        fail("The line profile is not closed.");
    }
    const std::size_t count = polygon.size();
    for (std::size_t first = 0; first < count; ++first) {
        for (std::size_t second = first + 1; second < count; ++second) {
            if (second == first + 1 || (first == 0 && second == count - 1)) {
                continue;
            }
            if (intersects(polygon[first], polygon[(first + 1) % count],
                           polygon[second], polygon[(second + 1) % count])) {
                fail("The profile intersects or touches itself. Use a simple closed boundary.");
            }
        }
    }
    // Subtract an anchor before accumulating area to avoid cancellation far from the origin.
    double twiceArea = 0.0;
    for (std::size_t index = 1; index + 1 < count; ++index) {
        twiceArea += cross(polygon[0], polygon[index], polygon[index + 1]);
    }
    if (std::abs(twiceArea) <= joiningTolerance * joiningTolerance) {
        fail("The profile has no usable enclosed area.");
    }
    if (twiceArea < 0.0) {
        std::reverse(polygon.begin(), polygon.end());
    }
    return polygon;
}

TopoDS_Face profileFace(const Sketch& sketch)
{
    validate(sketch);
    if (sketch.entities.empty()) {
        fail("The sketch is empty. Draw a closed profile before extruding.");
    }
    BRepBuilderAPI_MakeWire wire;
    if (sketch.entities.size() == 1 && sketch.entities.front().kind == EntityKind::Circle) {
        wire.Add(circleEdge(sketch.plane, sketch.entities.front()));
    } else {
        const auto points = sketch.entities.size() == 1
                && sketch.entities.front().kind == EntityKind::Rectangle
            ? rectanglePoints(sketch.entities.front()) : orderedLineLoop(sketch);
        for (std::size_t index = 0; index < points.size(); ++index) {
            wire.Add(lineEdge(sketch.plane, points[index], points[(index + 1) % points.size()]));
        }
    }
    if (!wire.IsDone()) {
        fail("The sketch boundary could not be assembled into a closed wire.");
    }
    BRepBuilderAPI_MakeFace face(gp_Pln(sketchAxes(sketch.plane)), wire.Wire(), Standard_True);
    if (!face.IsDone() || !BRepCheck_Analyzer(face.Face()).IsValid()) {
        fail("The sketch does not form a valid closed planar profile.");
    }
    return face.Face();
}

double volume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}

void requireSolid(const TopoDS_Shape& shape, const char* explanation)
{
    int count = 0;
    for (TopExp_Explorer explorer(shape, TopAbs_SOLID); explorer.More(); explorer.Next()) {
        ++count;
    }
    const double mass = shape.IsNull() ? 0.0 : volume(shape);
    if (shape.IsNull() || count != 1 || !std::isfinite(mass)
        || mass <= std::pow(Precision::Confusion(), 3)
        || !BRepCheck_Analyzer(shape).IsValid()) {
        fail(explanation);
    }
}

gp_Vec sweepVector(SketchPlane plane, double length, Direction direction)
{
    if (direction != Direction::Positive && direction != Direction::Negative) {
        fail("Unsupported extrusion direction.");
    }
    return gp_Vec(sketchAxes(plane).Direction()) * (direction == Direction::Positive ? length : -length);
}

TopoDS_Shape prism(const Sketch& sketch, double length, Direction direction)
{
    BRepPrimAPI_MakePrism builder(profileFace(sketch), sweepVector(sketch.plane, length, direction), Standard_True);
    if (!builder.IsDone()) {
        fail("Open CASCADE could not create the extrusion.");
    }
    const auto result = builder.Shape();
    requireSolid(result, "The profile did not generate one valid solid.");
    return result;
}

double throughLength(const TopoDS_Shape& base, SketchPlane plane, Direction direction)
{
    Bnd_Box box;
    BRepBndLib::Add(base, box);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const auto normal = sweepVector(plane, 1.0, direction);
    double furthest = 0.0;
    for (const double x : {xmin, xmax}) {
        for (const double y : {ymin, ymax}) {
            for (const double z : {zmin, zmax}) {
                furthest = std::max(furthest, gp_Vec(x, y, z).Dot(normal));
            }
        }
    }
    if (furthest <= Precision::Confusion()) {
        fail("The cut direction points away from the current solid.");
    }
    const double diagonal = std::hypot(std::hypot(xmax - xmin, ymax - ymin), zmax - zmin);
    return furthest + std::max(1.0, diagonal * 1e-6);
}
}

gp_Ax3 sketchAxes(SketchPlane plane)
{
    switch (plane) {
    case SketchPlane::XY:
        return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    case SketchPlane::XZ:
        return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
    case SketchPlane::YZ:
        return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
    }
    fail("Unsupported sketch plane.");
}

gp_Pnt sketchPoint(SketchPlane plane, Point2 point)
{
    const auto axes = sketchAxes(plane);
    return axes.Location().Translated(gp_Vec(axes.XDirection()) * point.x
                                    + gp_Vec(axes.YDirection()) * point.y);
}

TopoDS_Shape sketchShape(const Sketch& sketch)
{
    try {
        validate(sketch);
        BRep_Builder builder;
        TopoDS_Compound result;
        builder.MakeCompound(result);
        for (const auto& entity : sketch.entities) {
            if (entity.kind == EntityKind::Circle) {
                builder.Add(result, circleEdge(sketch.plane, entity));
            } else if (entity.kind == EntityKind::Line) {
                builder.Add(result, lineEdge(sketch.plane, entity.first, entity.second));
            } else {
                const auto points = rectanglePoints(entity);
                for (std::size_t index = 0; index < points.size(); ++index) {
                    builder.Add(result, lineEdge(sketch.plane, points[index], points[(index + 1) % points.size()]));
                }
            }
        }
        return result;
    } catch (const Standard_Failure& error) {
        fail(std::string("Cannot construct sketch geometry: ") + error.GetMessageString());
    }
}

void validateClosedProfile(const Sketch& sketch)
{
    try {
        (void)profileFace(sketch);
    } catch (const Standard_Failure& error) {
        fail(std::string("Invalid sketch profile: ") + error.GetMessageString());
    }
}

std::vector<Contour> closedContours(const Sketch& sketch)
{
    try {
        validate(sketch);
        std::vector<std::string> identifiers;
        std::set<std::string> distinct;
        for (std::size_t index = 0; index < sketch.entities.size(); ++index) {
            const auto& id = sketch.entities[index].id;
            const auto identifier = id.empty() ? "entity-" + std::to_string(index + 1) : id;
            if (!distinct.insert(identifier).second) {
                fail("Sketch entity IDs must be distinct before selecting a profile.");
            }
            identifiers.push_back(identifier);
        }
        std::vector<Contour> result;
        std::vector<bool> visited(sketch.entities.size(), false);
        for (std::size_t seed = 0; seed < sketch.entities.size(); ++seed) {
            if (visited[seed]) {
                continue;
            }
            std::vector<std::size_t> component{seed};
            visited[seed] = true;
            if (sketch.entities[seed].kind == EntityKind::Line) {
                // Extract connected line components; open or branched components
                // remain sketch geometry, but cannot become extrusion profiles.
                for (std::size_t next = 0; next < component.size(); ++next) {
                    const auto& current = sketch.entities[component[next]];
                    for (std::size_t index = 0; index < sketch.entities.size(); ++index) {
                        const auto& candidate = sketch.entities[index];
                        if (visited[index] || candidate.kind != EntityKind::Line) {
                            continue;
                        }
                        if (samePoint(current.first, candidate.first)
                            || samePoint(current.first, candidate.second)
                            || samePoint(current.second, candidate.first)
                            || samePoint(current.second, candidate.second)) {
                            visited[index] = true;
                            component.push_back(index);
                        }
                    }
                }
            }
            Sketch candidate;
            candidate.plane = sketch.plane;
            Contour contour;
            for (const auto index : component) {
                candidate.entities.push_back(sketch.entities[index]);
                contour.entityIds.push_back(identifiers[index]);
            }
            try {
                contour.face = profileFace(candidate);
            } catch (const std::runtime_error&) {
                continue;
            }
            result.push_back(std::move(contour));
        }
        // Adjacent, overlapping, or nested regions need region/inner-wire
        // semantics. Reject them explicitly until that workflow is designed.
        for (std::size_t first = 0; first < result.size(); ++first) {
            Bnd_Box firstBounds;
            BRepBndLib::Add(result[first].face, firstBounds);
            for (std::size_t second = first + 1; second < result.size(); ++second) {
                Bnd_Box secondBounds;
                BRepBndLib::Add(result[second].face, secondBounds);
                if (firstBounds.IsOut(secondBounds)) {
                    continue;
                }
                BRepExtrema_DistShapeShape separation(result[first].face, result[second].face);
                if (!separation.IsDone()) {
                    fail("The sketch regions could not be checked for overlap.");
                }
                if (separation.Value() <= joiningTolerance) {
                    fail("Nested, touching, or intersecting contours are not supported yet. Use separate non-overlapping simple closed profiles.");
                }
            }
        }
        return result;
    } catch (const Standard_Failure& error) {
        fail(std::string("Cannot identify sketch contours: ") + error.GetMessageString());
    }
}

Sketch selectContour(const Sketch& sketch, const std::vector<std::string>& entityIds)
{
    if (entityIds.empty()) {
        fail("Select a closed sketch region before creating a feature.");
    }
    const std::set<std::string> requested(entityIds.begin(), entityIds.end());
    if (requested.size() != entityIds.size()) {
        fail("A profile selection contains duplicate entity IDs.");
    }
    for (const auto& contour : closedContours(sketch)) {
        const std::set<std::string> candidate(contour.entityIds.begin(), contour.entityIds.end());
        if (candidate != requested) {
            continue;
        }
        Sketch result;
        result.plane = sketch.plane;
        for (std::size_t index = 0; index < sketch.entities.size(); ++index) {
            const auto& entity = sketch.entities[index];
            const auto id = entity.id.empty() ? "entity-" + std::to_string(index + 1) : entity.id;
            if (requested.count(id) != 0) {
                auto copy = entity;
                copy.id = id;
                result.entities.push_back(std::move(copy));
            }
        }
        return result;
    }
    fail("The selected entity IDs no longer describe one valid closed contour. Select a region again.");
}

bool contourContains(const Contour& contour, SketchPlane plane, Point2 point)
{
    if (contour.face.IsNull()) {
        return false;
    }
    finiteCoordinate(point.x);
    finiteCoordinate(point.y);
    BRepClass_FaceClassifier classifier(contour.face, sketchPoint(plane, point), joiningTolerance);
    return classifier.State() == TopAbs_IN || classifier.State() == TopAbs_ON;
}
TopoDS_Shape makeExtrusion(const Sketch& sketch, double length, Direction direction,
                          const TopoDS_Shape& base)
{
    try {
        positiveLength(length, "Extrusion length");
        auto result = prism(sketch, length, direction);
        if (!base.IsNull()) {
            const auto privateBase = BRepBuilderAPI_Copy(base, Standard_True, Standard_False).Shape();
            requireSolid(privateBase, "The existing body is not one valid solid.");
            TopTools_ListOfShape arguments, tools;
            arguments.Append(privateBase);
            tools.Append(result);
            BRepAlgoAPI_Fuse operation;
            operation.SetArguments(arguments);
            operation.SetTools(tools);
            operation.SetNonDestructive(Standard_True);
            operation.Build();
            if (!operation.IsDone() || operation.HasErrors()) {
                fail("Open CASCADE could not fuse the extrusion with the current body.");
            }
            operation.SimplifyResult();
            result = operation.Shape();
            requireSolid(result, "The extrusion must touch the current body and produce one connected solid.");
        }
        return result;
    } catch (const Standard_Failure& error) {
        fail(std::string("Cannot extrude the sketch: ") + error.GetMessageString());
    }
}

TopoDS_Shape makeCut(const TopoDS_Shape& base, const Sketch& sketch, double depth,
                    bool throughAll, Direction direction)
{
    try {
        if (base.IsNull()) {
            fail("An extruded cut requires an existing solid.");
        }
        // OCCT may update shared topological flags even in non-destructive mode.
        // Own the input topology and geometry so canceled previews cannot touch it.
        const auto privateBase = BRepBuilderAPI_Copy(base, Standard_True, Standard_False).Shape();
        requireSolid(privateBase, "An extruded cut requires one valid existing solid.");
        positiveLength(depth, "Cut depth");
        const double length = throughAll ? throughLength(privateBase, sketch.plane, direction) : depth;
        const auto tool = prism(sketch, length, direction);
        const double beforeVolume = volume(privateBase);
        TopTools_ListOfShape arguments, tools;
        arguments.Append(privateBase);
        tools.Append(tool);
        BRepAlgoAPI_Cut operation;
        operation.SetArguments(arguments);
        operation.SetTools(tools);
        operation.SetNonDestructive(Standard_True);
        operation.Build();
        if (!operation.IsDone() || operation.HasErrors()) {
            fail("Open CASCADE could not subtract the sketch extrusion from the body.");
        }
        operation.SimplifyResult();
        const auto result = operation.Shape();
        requireSolid(result, "The cut must leave one valid solid; it cannot remove the whole body or split it into separate bodies.");
        const double afterVolume = volume(result);
        if (beforeVolume - afterVolume <= std::max(beforeVolume * 1e-10, 1e-15)) {
            fail("The cut does not remove material. Check the sketch location, depth, and direction.");
        }
        return result;
    } catch (const Standard_Failure& error) {
        fail(std::string("Cannot cut the body: ") + error.GetMessageString());
    }
}


namespace {
using PointKey = std::pair<std::string, int>;
PointKey pointKey(const SketchPointRef& ref) { return {ref.entityId, static_cast<int>(ref.role)}; }
bool stableIdentifier(const std::string& id)
{
    return !id.empty() && id.size() <= 128 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
struct EqualityGroups {
    std::vector<std::pair<SketchPointRef, Point2>> points;
    std::map<PointKey, std::size_t> indices;
    std::vector<std::size_t> x, y;
    explicit EqualityGroups(const Sketch& sketch) : points(sketchPoints(sketch)), x(points.size()), y(points.size())
    {
        std::iota(x.begin(), x.end(), 0);
        std::iota(y.begin(), y.end(), 0);
        for (std::size_t i = 0; i < points.size(); ++i) indices.emplace(pointKey(points[i].first), i);
    }
    static std::size_t root(std::vector<std::size_t>& parents, std::size_t index) {
        if (parents[index] != index) parents[index] = root(parents, parents[index]);
        return parents[index];
    }
    std::size_t index(const SketchPointRef& ref) const {
        const auto found = indices.find(pointKey(ref));
        if (found == indices.end()) fail("Sketch relation refers to a missing entity or invalid point role.");
        return found->second;
    }
    void join(const SketchRelation& relation) {
        const auto a = index(relation.first), b = index(relation.second);
        if (a == b) fail("A sketch relation must refer to two distinct points.");
        if (relation.kind == RelationKind::Coincident || relation.kind == RelationKind::Vertical)
            x[root(x, a)] = root(x, b);
        if (relation.kind == RelationKind::Coincident || relation.kind == RelationKind::Horizontal)
            y[root(y, a)] = root(y, b);
    }
};
void setPoint(Sketch& sketch, const SketchPointRef& ref, Point2 point) {
    if (ref.role == PointRole::Origin) return;
    auto found = std::find_if(sketch.entities.begin(), sketch.entities.end(), [&](const SketchEntity& entity) { return entity.id == ref.entityId; });
    if (found == sketch.entities.end()) fail("Cannot update a missing sketch point.");
    if (ref.role == PointRole::Second) found->second = point;
    else found->first = point;
}
}

Point2 sketchPointValue(const Sketch& sketch, const SketchPointRef& reference)
{
    if (reference.role == PointRole::Origin) {
        if (!reference.entityId.empty()) fail("The sketch origin cannot have an entity ID.");
        return {0, 0};
    }
    if (reference.entityId.empty()) fail("A sketch point requires a stable entity ID.");
    const auto found = std::find_if(sketch.entities.begin(), sketch.entities.end(), [&](const SketchEntity& entity) { return entity.id == reference.entityId; });
    if (found == sketch.entities.end()) fail("Sketch point refers to a missing entity.");
    if (reference.role == PointRole::Center && found->kind == EntityKind::Circle) return found->first;
    if (found->kind == EntityKind::Line || found->kind == EntityKind::Rectangle) {
        if (reference.role == PointRole::First) return found->first;
        if (reference.role == PointRole::Second) return found->second;
    }
    fail("Sketch point role does not belong to this entity type.");
}

std::vector<std::pair<SketchPointRef, Point2>> sketchPoints(const Sketch& sketch)
{
    std::vector<std::pair<SketchPointRef, Point2>> points{{SketchPointRef{}, Point2{}}};
    std::set<std::string> ids;
    for (const auto& entity : sketch.entities) {
        // Relation-free legacy construction geometry may not yet have IDs.
        if (entity.id.empty()) continue;
        if (!stableIdentifier(entity.id) || !ids.insert(entity.id).second) fail("Sketch entity IDs must be valid and unique.");
        if (entity.kind == EntityKind::Circle) points.push_back({{entity.id, PointRole::Center}, entity.first});
        else {
            points.push_back({{entity.id, PointRole::First}, entity.first});
            points.push_back({{entity.id, PointRole::Second}, entity.second});
        }
    }
    return points;
}

void validateSketchRelations(const Sketch& sketch)
{
    if (sketch.relations.size() > 4000) fail("This prototype supports at most 4000 relations per sketch.");
    EqualityGroups groups(sketch);
    std::set<std::string> ids;
    std::set<std::tuple<int, PointKey, PointKey>> signatures;
    for (const auto& relation : sketch.relations) {
        if (!stableIdentifier(relation.id) || !ids.insert(relation.id).second) fail("Sketch relation IDs must be valid and unique.");
        const auto first = sketchPointValue(sketch, relation.first), second = sketchPointValue(sketch, relation.second);
        if (relation.kind == RelationKind::Horizontal || relation.kind == RelationKind::Vertical) {
            if (relation.first.entityId != relation.second.entityId || relation.first.entityId.empty()
                || !((relation.first.role == PointRole::First && relation.second.role == PointRole::Second)
                    || (relation.first.role == PointRole::Second && relation.second.role == PointRole::First)))
                fail("Horizontal and vertical relations require the two endpoints of one line.");
            const auto entity = std::find_if(sketch.entities.begin(), sketch.entities.end(), [&](const SketchEntity& e) { return e.id == relation.first.entityId; });
            if (entity == sketch.entities.end() || entity->kind != EntityKind::Line) fail("Horizontal and vertical relations apply to line entities only.");
        } else if (relation.kind != RelationKind::Coincident) fail("Unsupported sketch relation type.");
        auto a = pointKey(relation.first), b = pointKey(relation.second);
        if (b < a) std::swap(a, b);
        if (!signatures.emplace(static_cast<int>(relation.kind), a, b).second) fail("Duplicate sketch relation.");
        groups.join(relation);
        if ((relation.kind == RelationKind::Coincident || relation.kind == RelationKind::Vertical) && first.x != second.x)
            fail("Sketch relation is not satisfied: connected X coordinates must be identical.");
        if ((relation.kind == RelationKind::Coincident || relation.kind == RelationKind::Horizontal) && first.y != second.y)
            fail("Sketch relation is not satisfied: connected Y coordinates must be identical.");
    }
}

Sketch editSketchEntity(const Sketch& sketch, const SketchEntity& replacement)
{
    validate(sketch);
    auto candidate = sketch;
    const auto old = std::find_if(sketch.entities.begin(), sketch.entities.end(), [&](const SketchEntity& entity) { return entity.id == replacement.id; });
    if (replacement.id.empty() || old == sketch.entities.end()) fail("Cannot edit a sketch entity without its existing stable ID.");
    if (old->kind != replacement.kind) fail("Editing cannot change a sketch entity type.");
    *std::find_if(candidate.entities.begin(), candidate.entities.end(), [&](const SketchEntity& entity) { return entity.id == replacement.id; }) = replacement;
    EqualityGroups groups(sketch);
    for (const auto& relation : sketch.relations) groups.join(relation);
    std::map<std::size_t, double> anchorsX, anchorsY;
    const auto anchor = [](auto& anchors, std::size_t group, double value) {
        if (!std::isfinite(value) || std::abs(value) > coordinateLimit) fail("Sketch coordinates must be finite and within the prototype limit.");
        const auto found = anchors.find(group);
        if (found != anchors.end() && found->second != value)
            fail("The edit conflicts with a coincident, horizontal, vertical, or fixed-origin relation.");
        anchors[group] = value;
    };
    anchor(anchorsX, EqualityGroups::root(groups.x, 0), 0);
    anchor(anchorsY, EqualityGroups::root(groups.y, 0), 0);
    for (std::size_t i = 1; i < groups.points.size(); ++i) {
        const auto& ref = groups.points[i].first;
        if (ref.entityId != replacement.id) continue;
        const auto before = groups.points[i].second, after = sketchPointValue(candidate, ref);
        if (before.x != after.x) anchor(anchorsX, EqualityGroups::root(groups.x, i), after.x);
        if (before.y != after.y) anchor(anchorsY, EqualityGroups::root(groups.y, i), after.y);
    }
    for (std::size_t i = 1; i < groups.points.size(); ++i) {
        auto point = sketchPointValue(candidate, groups.points[i].first);
        const auto x = anchorsX.find(EqualityGroups::root(groups.x, i)), y = anchorsY.find(EqualityGroups::root(groups.y, i));
        if (x != anchorsX.end()) point.x = x->second;
        if (y != anchorsY.end()) point.y = y->second;
        setPoint(candidate, groups.points[i].first, point);
    }
    validate(candidate);
    return candidate;
}

std::vector<SketchPointRef> coincidentSketchPoints(const Sketch& sketch,const SketchPointRef& reference)
{
    EqualityGroups groups(sketch);
    for(const auto& relation:sketch.relations) if(relation.kind==RelationKind::Coincident) groups.join(relation);
    const auto root=EqualityGroups::root(groups.x,groups.index(reference));
    std::vector<SketchPointRef> result;
    for(std::size_t i=0;i<groups.points.size();++i)
        if(EqualityGroups::root(groups.x,i)==root) result.push_back(groups.points[i].first);
    return result;
}

bool areSketchPointsCoincident(const Sketch& sketch, const SketchPointRef& first, const SketchPointRef& second)
{
    EqualityGroups groups(sketch);
    for(const auto& relation:sketch.relations)
        if(relation.kind==RelationKind::Coincident) groups.join(relation);
    return EqualityGroups::root(groups.x,groups.index(first)) == EqualityGroups::root(groups.x,groups.index(second));
}

Sketch moveSketchPoint(const Sketch& sketch, const SketchPointRef& reference, Point2 position,
                      const std::optional<SketchPointRef>& target)
{
    validate(sketch);
    if(reference.role==PointRole::Origin) fail("The sketch origin is fixed and cannot be dragged.");
    (void)sketchPointValue(sketch,reference);
    if(target) {
        if(pointKey(reference)==pointKey(*target)) fail("A point cannot be joined to itself.");
        position=sketchPointValue(sketch,*target);
    }
    auto entity=*std::find_if(sketch.entities.begin(),sketch.entities.end(),[&](const SketchEntity& e){return e.id==reference.entityId;});
    if(reference.role==PointRole::Second) entity.second=position;
    else entity.first=position;
    auto candidate=editSketchEntity(sketch,entity);
    if(target && !areSketchPointsCoincident(candidate,reference,*target)) {
        std::set<std::string> ids;
        for(const auto& relation:candidate.relations) ids.insert(relation.id);
        int sequence=1; while(ids.count("connection-"+std::to_string(sequence))) ++sequence;
        candidate.relations.push_back({"connection-"+std::to_string(sequence),RelationKind::Coincident,reference,*target});
    }
    validate(candidate);
    return candidate;
}

Sketch removeSketchEntity(const Sketch& sketch, const std::string& entityId)
{
    auto candidate = sketch;
    const auto found = std::find_if(candidate.entities.begin(), candidate.entities.end(), [&](const SketchEntity& entity) { return entity.id == entityId; });
    if (entityId.empty() || found == candidate.entities.end()) fail("Cannot remove an unknown sketch entity.");
    candidate.entities.erase(found);
    candidate.relations.erase(std::remove_if(candidate.relations.begin(), candidate.relations.end(), [&](const SketchRelation& relation) {
        return relation.first.entityId == entityId || relation.second.entityId == entityId;
    }), candidate.relations.end());
    validate(candidate);
    return candidate;
}

} // namespace thecad::modeling
