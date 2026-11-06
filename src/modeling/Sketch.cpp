#include "modeling/RelationCommands.h"
#include "modeling/PlanarRegions.h"
#include "modeling/SketchTools.h"
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <functional>
#include <BRepAdaptor_Surface.hxx>
#include "modeling/Sketch.h"
#include "modeling/ConstraintSolver.h"

#include <BRep_Builder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
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
    if(sketch.space!=SketchSpace::Planar2D)fail("Spatial 3D sketches require a future solver/backend milestone.");
    (void)sketchAxes(sketch);
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
        case EntityKind::Point: break;
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

TopoDS_Edge lineEdge(const Sketch& plane, Point2 first, Point2 second)
{
    BRepBuilderAPI_MakeEdge builder(sketchPoint(plane, first), sketchPoint(plane, second));
    if (!builder.IsDone()) {
        fail("The line could not be constructed by the geometry kernel.");
    }
    return builder.Edge();
}

TopoDS_Edge circleEdge(const Sketch& plane, const SketchEntity& entity)
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

TopoDS_Face profileFace(const Sketch& sketch);
TopoDS_Face simpleProfileFace(const Sketch& sketch,TopologyNames* names=nullptr)
{
    validate(sketch);
    if(std::any_of(sketch.entities.begin(),sketch.entities.end(),[](const auto& e){return e.construction || e.kind==EntityKind::Point;})){
        auto boundary=sketch;boundary.entities.erase(std::remove_if(boundary.entities.begin(),boundary.entities.end(),[](const auto& e){return e.construction || e.kind==EntityKind::Point;}),boundary.entities.end());
        boundary.relations.clear();boundary.dimensions.clear();boundary.polygons.clear();return profileFace(boundary);
    }
    if (sketch.entities.empty()) {
        fail("The sketch is empty. Draw a closed profile before extruding.");
    }
    BRepBuilderAPI_MakeWire wire;
    if (sketch.entities.size() == 1 && sketch.entities.front().kind == EntityKind::Circle) {
        const auto edge=circleEdge(sketch,sketch.entities.front());wire.Add(edge);if(names)names->add(topologyKey("curve",{sketch.entities.front().id}),wire.Edge());
    } else {
        const auto points = sketch.entities.size() == 1
                && sketch.entities.front().kind == EntityKind::Rectangle
            ? rectanglePoints(sketch.entities.front()) : orderedLineLoop(sketch);
        for (std::size_t index = 0; index < points.size(); ++index) {
            wire.Add(lineEdge(sketch, points[index], points[(index + 1) % points.size()]));
            if(names){std::string source;if(sketch.entities.size()==1)source=sketch.entities.front().id+"/side/"+std::to_string(index);else for(const auto& e:sketch.entities)if((samePoint(e.first,points[index])&&samePoint(e.second,points[(index+1)%points.size()]))||(samePoint(e.second,points[index])&&samePoint(e.first,points[(index+1)%points.size()]))) {if(!source.empty())fail("Ambiguous sketch edge provenance.");source=e.id;}if(!source.empty())names->add(topologyKey("curve",{source}),wire.Edge());}
        }
    }
    if (!wire.IsDone()) {
        fail("The sketch boundary could not be assembled into a closed wire.");
    }
    BRepBuilderAPI_MakeFace face(gp_Pln(sketchAxes(sketch)), wire.Wire(), Standard_True);
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

void requireSolids(const TopoDS_Shape& shape, const char* explanation)
{
    int count = 0;
    for (TopExp_Explorer explorer(shape, TopAbs_SOLID); explorer.More(); explorer.Next()) {
        ++count;
    }
    const double mass = shape.IsNull() ? 0.0 : volume(shape);
    if (shape.IsNull() || count < 1 || !std::isfinite(mass)
        || mass <= std::pow(Precision::Confusion(), 3)
        || !BRepCheck_Analyzer(shape).IsValid()) {
        fail(explanation);
    }
}

void requireSolid(const TopoDS_Shape& shape,const char* explanation)
{
    requireSolids(shape,explanation);
    int count=0;for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next())++count;
    if(count!=1)fail(explanation);
}

gp_Vec sweepVector(const Sketch& plane, double length, Direction direction)
{
    if (direction != Direction::Positive && direction != Direction::Negative) {
        fail("Unsupported extrusion direction.");
    }
    return gp_Vec(sketchAxes(plane).Direction()) * (direction == Direction::Positive ? length : -length);
}

TopoDS_Shape prism(const Sketch& sketch, double length, Direction direction)
{
    BRepPrimAPI_MakePrism builder(profileFace(sketch), sweepVector(sketch, length, direction), Standard_True);
    if (!builder.IsDone()) {
        fail("Open CASCADE could not create the extrusion.");
    }
    const auto result = builder.Shape();
    requireSolid(result, "The profile did not generate one valid solid.");
    return result;
}

double throughLength(const TopoDS_Shape& base, const Sketch& plane, Direction direction)
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
                furthest = std::max(furthest, gp_Vec(sketchAxes(plane).Location(),gp_Pnt(x,y,z)).Dot(normal));
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

gp_Ax3 sketchAxes(const Sketch& sketch) { return !sketch.referencePlaneId.empty()?sketch.referencePlaneFrame:sketch.attachment ? sketch.attachment->frame : sketchAxes(sketch.plane); }
gp_Pnt sketchPoint(const Sketch& sketch, Point2 point) {
    const auto axes=sketchAxes(sketch);
    return axes.Location().Translated(gp_Vec(axes.XDirection())*point.x+gp_Vec(axes.YDirection())*point.y);
}
gp_Ax3 planarFaceAxes(const TopoDS_Face& face) {
    BRepAdaptor_Surface surface(face);
    if(surface.GetType()!=GeomAbs_Plane) fail("Select a planar solid face; curved faces cannot support a sketch yet.");
    auto normal=surface.Plane().Axis().Direction();
    if(face.Orientation()==TopAbs_REVERSED) normal.Reverse();
    GProp_GProps properties;BRepGProp::SurfaceProperties(face,properties);
    gp_Vec x=std::abs(normal.X())<0.9 ? gp_Vec(1,0,0) : gp_Vec(0,1,0);
    x-=gp_Vec(normal)*x.Dot(gp_Vec(normal));
    return gp_Ax3(properties.CentreOfMass(),normal,gp_Dir(x));
}
bool contourContains(const Contour& contour,const Sketch& sketch,Point2 point) {
    if(contour.face.IsNull()) return false;
    finiteCoordinate(point.x);finiteCoordinate(point.y);
    BRepClass_FaceClassifier classifier(contour.face,sketchPoint(sketch,point),joiningTolerance);
    return classifier.State()==TopAbs_IN || classifier.State()==TopAbs_ON;
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
                builder.Add(result, circleEdge(sketch, entity));
            } else if (entity.kind == EntityKind::Line) {
                builder.Add(result, lineEdge(sketch, entity.first, entity.second));
            } else if(entity.kind==EntityKind::Point) {
                continue; // Point markers are presented separately, never as profile edges.
            } else {
                const auto points = rectanglePoints(entity);
                for (std::size_t index = 0; index < points.size(); ++index) {
                    builder.Add(result, lineEdge(sketch, points[index], points[(index + 1) % points.size()]));
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
        if(std::any_of(sketch.entities.begin(),sketch.entities.end(),[](const auto& e){return e.construction || e.kind==EntityKind::Point;})){
            auto boundary=sketch;for(std::size_t i=0;i<boundary.entities.size();++i)if(boundary.entities[i].id.empty())boundary.entities[i].id="entity-"+std::to_string(i+1);
            boundary.entities.erase(std::remove_if(boundary.entities.begin(),boundary.entities.end(),[](const auto& e){return e.construction || e.kind==EntityKind::Point;}),boundary.entities.end());
            boundary.relations.clear();boundary.dimensions.clear();boundary.polygons.clear();
            return closedContours(boundary);
        }
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
        // Endpoint planar graph. Bridges cannot bound a region; remove them from
        // the analysis only, retaining every original entity in the document.
        std::vector<Point2> nodes;
        std::vector<std::vector<int>> incident;
        struct Edge {int a,b;std::size_t entity;};std::vector<Edge> edges;
        const auto nodeFor=[&](Point2 point) {
            for(std::size_t i=0;i<nodes.size();++i)if(samePoint(nodes[i],point))return int(i);
            nodes.push_back(point);incident.emplace_back();return int(nodes.size()-1);
        };
        for(std::size_t i=0;i<sketch.entities.size();++i) {
            const auto& e=sketch.entities[i];if(e.kind!=EntityKind::Line)continue;
            const int a=nodeFor(e.first),b=nodeFor(e.second),index=int(edges.size());
            edges.push_back({a,b,i});incident[a].push_back(index);incident[b].push_back(index);
        }
        // Keep established whole-edge references for simple profiles. Crossings
        // and T-junctions use an exact derived arrangement without editing entities.
        for(std::size_t i=0;i<edges.size();++i)for(std::size_t j=i+1;j<edges.size();++j) {
            const auto a=nodes[edges[i].a],b=nodes[edges[i].b],c=nodes[edges[j].a],d=nodes[edges[j].b];
            if(!intersects(a,b,c,d))continue;
            const bool shared=samePoint(a,c)||samePoint(a,d)||samePoint(b,c)||samePoint(b,d);
            const bool interior=(onSegment(a,b,c)&&!samePoint(c,a)&&!samePoint(c,b)) ||
                (onSegment(a,b,d)&&!samePoint(d,a)&&!samePoint(d,b)) ||
                (onSegment(c,d,a)&&!samePoint(a,c)&&!samePoint(a,d)) ||
                (onSegment(c,d,b)&&!samePoint(b,c)&&!samePoint(b,d));
            if(!shared || interior || (samePoint(a,c)&&samePoint(b,d)) || (samePoint(a,d)&&samePoint(b,c)))
                return intersectedContours(sketch);
        }
        for(const auto& line:sketch.entities)if(line.kind==EntityKind::Line)for(const auto& primitive:sketch.entities) {
            if(primitive.kind==EntityKind::Rectangle) {
                const auto corners=rectanglePoints(primitive);
                for(int i=0;i<4;++i) {
                    const auto a=corners[i],b=corners[(i+1)%4];
                    if(!intersects(a,b,line.first,line.second))continue;
                    const bool corner=samePoint(a,line.first)||samePoint(a,line.second)||samePoint(b,line.first)||samePoint(b,line.second);
                    const bool overlap=(onSegment(a,b,line.first)&&!samePoint(a,line.first)&&!samePoint(b,line.first)) ||
                        (onSegment(a,b,line.second)&&!samePoint(a,line.second)&&!samePoint(b,line.second));
                    const bool covers=(onSegment(line.first,line.second,a)&&!samePoint(a,line.first)&&!samePoint(a,line.second)) ||
                        (onSegment(line.first,line.second,b)&&!samePoint(b,line.first)&&!samePoint(b,line.second)) ||
                        (samePoint(a,line.first)&&samePoint(b,line.second)) || (samePoint(b,line.first)&&samePoint(a,line.second));
                    if(!corner || overlap || covers)
                        return intersectedContours(sketch);
                }
            } else if(primitive.kind==EntityKind::Circle) {
                const double dx=line.second.x-line.first.x,dy=line.second.y-line.first.y;
                const double ox=line.first.x-primitive.first.x,oy=line.first.y-primitive.first.y;
                const double a=dx*dx+dy*dy,b=2*(ox*dx+oy*dy),c=ox*ox+oy*oy-primitive.radius*primitive.radius;
                const double discriminant=b*b-4*a*c;
                if(discriminant>=0)for(double t:{(-b-std::sqrt(discriminant))/(2*a),(-b+std::sqrt(discriminant))/(2*a)})
                    if(t>=-1e-9 && t<=1+1e-9)return intersectedContours(sketch);
            }
        }
        std::vector<int> discovered(nodes.size(),-1),low(nodes.size());std::vector<bool> bridge(edges.size(),false);int clock=0;
        std::function<void(int,int)> visit=[&](int node,int parentEdge) {
            discovered[node]=low[node]=clock++;
            for(int index:incident[node]) {
                if(index==parentEdge)continue;const auto& e=edges[index];const int next=e.a==node?e.b:e.a;
                if(discovered[next]<0) {visit(next,index);low[node]=std::min(low[node],low[next]);if(low[next]>discovered[node])bridge[index]=true;}
                else low[node]=std::min(low[node],discovered[next]);
            }
        };
        for(std::size_t i=0;i<nodes.size();++i)if(discovered[i]<0)visit(int(i),-1);
        std::vector<Contour> result;
        const auto append=[&](const std::vector<std::size_t>& entities) {
            Sketch candidate;candidate.plane=sketch.plane;candidate.attachment=sketch.attachment;candidate.referencePlaneId=sketch.referencePlaneId;candidate.referencePlaneFrame=sketch.referencePlaneFrame;Contour contour;
            for(auto i:entities){candidate.entities.push_back(sketch.entities[i]);contour.entityIds.push_back(identifiers[i]);}
            contour.face=simpleProfileFace(candidate,&contour.boundaryNames);result.push_back(std::move(contour));
        };
        // Traverse the left face of each directed edge in the planar embedding.
        // The clockwise successor of the reverse edge keeps a bounded face on
        // the left. Shared edges participate in both adjacent bounded regions.
        std::vector<std::vector<int>> outgoing(nodes.size());
        for(std::size_t i=0;i<edges.size();++i)if(!bridge[i]) {
            outgoing[edges[i].a].push_back(int(i)*2);
            outgoing[edges[i].b].push_back(int(i)*2+1);
        }
        const auto from=[&](int half){const auto& e=edges[std::size_t(half/2)];return half%2?e.b:e.a;};
        const auto to=[&](int half){const auto& e=edges[std::size_t(half/2)];return half%2?e.a:e.b;};
        for(auto& list:outgoing)std::sort(list.begin(),list.end(),[&](int a,int b){
            const auto origin=nodes[from(a)],pa=nodes[to(a)],pb=nodes[to(b)];
            return std::atan2(pa.y-origin.y,pa.x-origin.x)<std::atan2(pb.y-origin.y,pb.x-origin.x);
        });
        std::vector<bool> visited(edges.size()*2,false);
        for(std::size_t seed=0;seed<visited.size();++seed) {
            if(visited[seed] || bridge[seed/2])continue;
            std::vector<std::size_t> entities;std::set<int> vertices;double twiceArea=0;int current=int(seed);
            do {
                if(visited[std::size_t(current)])fail("The sketch graph cannot form a closed planar region.");
                visited[std::size_t(current)]=true;
                const int a=from(current),b=to(current);vertices.insert(a);
                twiceArea+=nodes[a].x*nodes[b].y-nodes[b].x*nodes[a].y;
                entities.push_back(edges[std::size_t(current/2)].entity);
                const auto& choices=outgoing[b];const auto reverse=std::find(choices.begin(),choices.end(),current^1);
                if(reverse==choices.end() || choices.empty())fail("Incomplete planar sketch boundary.");
                const auto index=std::size_t(reverse-choices.begin());current=choices[(index+choices.size()-1)%choices.size()];
            }while(current!=int(seed));
            if(twiceArea<=joiningTolerance*joiningTolerance)continue; // exterior/unbounded walk
            if(vertices.size()!=entities.size())fail("A bounded region has an invalid repeated vertex.");
            std::sort(entities.begin(),entities.end());append(entities);
        }
        for(std::size_t i=0;i<sketch.entities.size();++i)if(sketch.entities[i].kind!=EntityKind::Line)append({i});
        // Boundary contact/crossing is ambiguous; strict containment is not.
        // Each simple loop defines a selectable bounded region with its direct
        // children removed as holes. Inner regions remain selectable separately.
        const auto simple=result;
        std::vector<int> parent(simple.size(),-1);std::vector<double> areas;
        for(const auto& region:simple){GProp_GProps p;BRepGProp::SurfaceProperties(region.face,p);areas.push_back(p.Mass());}
        const auto pointOnBoundary=[](const TopoDS_Face& face) {
            const auto wire=BRepTools::OuterWire(face);TopExp_Explorer edges(wire,TopAbs_EDGE);
            if(!edges.More())fail("A closed region has no valid boundary.");
            return BRep_Tool::Pnt(TopExp::FirstVertex(TopoDS::Edge(edges.Current())));
        };
        for(std::size_t i=0;i<simple.size();++i)for(std::size_t j=i+1;j<simple.size();++j) {
            Bnd_Box aBounds,bBounds;BRepBndLib::Add(simple[i].face,aBounds);BRepBndLib::Add(simple[j].face,bBounds);
            if(aBounds.IsOut(bBounds))continue;
            BRepExtrema_DistShapeShape distance(BRepTools::OuterWire(simple[i].face),BRepTools::OuterWire(simple[j].face));
            if(!distance.IsDone())fail("Cannot validate sketch boundaries.");
            if(distance.Value()<=joiningTolerance) {
                const auto linesOnly=[&](std::size_t index){return std::all_of(simple[index].entityIds.begin(),simple[index].entityIds.end(),[&](const auto& id){const auto found=std::find(identifiers.begin(),identifiers.end(),id);return found!=identifiers.end() && sketch.entities[std::size_t(found-identifiers.begin())].kind==EntityKind::Line;});};
                if(!linesOnly(i) || !linesOnly(j))return intersectedContours(sketch);
                BRepAlgoAPI_Common common(simple[i].face,simple[j].face);common.SetNonDestructive(Standard_True);common.Build();
                if(!common.IsDone() || common.HasErrors())fail("Cannot validate adjacent sketch regions.");
                GProp_GProps overlap;BRepGProp::SurfaceProperties(common.Shape(),overlap);
                if(overlap.Mass()>joiningTolerance*joiningTolerance)fail("Adjacent regions have overlapping interiors.");
                continue; // Valid adjacent cells cannot contain each other's interior.
            }
            for(const auto pair:{std::pair<std::size_t,std::size_t>{i,j},{j,i}}) {
                BRepClass_FaceClassifier classifier(simple[pair.first].face,pointOnBoundary(simple[pair.second].face),joiningTolerance);
                if(classifier.State()==TopAbs_IN && (parent[pair.second]<0 || areas[pair.first]<areas[std::size_t(parent[pair.second])]))parent[pair.second]=int(pair.first);
            }
        }
        for(std::size_t i=0;i<simple.size();++i) {
            BRepBuilderAPI_MakeFace builder(gp_Pln(sketchAxes(sketch)),BRepTools::OuterWire(simple[i].face),Standard_True);
            result[i].entityIds=simple[i].entityIds;
            // Adjacent child cells form one hole boundary. Shared internal edges
            // cancel; retaining each child wire would create overlapping holes.
            std::vector<std::size_t> children;for(std::size_t j=0;j<simple.size();++j)if(parent[j]==int(i))children.push_back(j);
            std::vector<int> groups(children.size());std::iota(groups.begin(),groups.end(),0);
            const auto root=[&](int index){while(groups[std::size_t(index)]!=index)index=groups[std::size_t(index)];return index;};
            std::map<std::string,int> owners;
            for(std::size_t n=0;n<children.size();++n)for(const auto& id:simple[children[n]].entityIds){const auto found=owners.find(id);if(found==owners.end())owners[id]=int(n);else groups[std::size_t(root(int(n)))]=root(found->second);}
            std::map<int,std::map<std::string,int>> boundaries;
            for(std::size_t n=0;n<children.size();++n)for(const auto& id:simple[children[n]].entityIds)++boundaries[root(int(n))][id];
            for(const auto& group:boundaries) {
                Sketch boundary;boundary.plane=sketch.plane;boundary.attachment=sketch.attachment;boundary.referencePlaneId=sketch.referencePlaneId;boundary.referencePlaneFrame=sketch.referencePlaneFrame;
                for(std::size_t index=0;index<identifiers.size();++index){const auto found=group.second.find(identifiers[index]);if(found!=group.second.end() && found->second==1){boundary.entities.push_back(sketch.entities[index]);result[i].entityIds.push_back(identifiers[index]);}}
                TopologyNames holeNames;const auto hole=simpleProfileFace(boundary,&holeNames);for(const auto& [key,list]:holeNames.images)for(const auto& edge:list)result[i].boundaryNames.add(key,edge);builder.Add(TopoDS::Wire(BRepTools::OuterWire(hole).Reversed()));
            }
            if(!builder.IsDone() || !BRepCheck_Analyzer(builder.Face()).IsValid())fail("The nested boundaries do not form a valid planar region.");
            result[i].face=builder.Face();
        }
        return result;
    } catch (const Standard_Failure& error) {
        fail(std::string("Cannot identify sketch contours: ") + error.GetMessageString());
    }
}

namespace {
TopoDS_Face profileFace(const Sketch& sketch)
{
    std::set<std::string> requested;
    for(std::size_t i=0;i<sketch.entities.size();++i)requested.insert(sketch.entities[i].id.empty()?"entity-"+std::to_string(i+1):sketch.entities[i].id);
    for(const auto& region:closedContours(sketch))if(std::set<std::string>(region.entityIds.begin(),region.entityIds.end())==requested)return region.face;
    fail("Select one or more closed regions explicitly; open geometry and unrelated regions are not an extrusion boundary.");
}
std::vector<TopoDS_Shape> regionPrisms(const Sketch& sketch,const RegionReferences& references,double length,Direction direction,TopologyNames* names=nullptr,const std::string& featureId={})
{
    if(references.empty())fail("Select at least one closed region.");
    const auto regions=closedContours(sketch);std::set<std::set<std::string>> selected;std::vector<TopoDS_Shape> tools;
    for(const auto& ids:references) {
        const std::set<std::string> key(ids.begin(),ids.end());
        if(key.empty() || key.size()!=ids.size() || !selected.insert(key).second)fail("Selected regions contain empty or duplicate references.");
        const auto found=std::find_if(regions.begin(),regions.end(),[&](const auto& region){return std::set<std::string>(region.entityIds.begin(),region.entityIds.end())==key;});
        if(found==regions.end())fail("A selected region changed or disappeared. Reselect it explicitly.");
        BRepPrimAPI_MakePrism prism(found->face,sweepVector(sketch,length,direction),Standard_True);
        if(!prism.IsDone())fail("Cannot extrude a selected region.");
        requireSolid(prism.Shape(),"A selected region did not produce a valid solid.");if(names){auto regionIds=found->entityIds;regionIds.push_back(featureId);const auto regionKey=topologyKey("region",regionIds);names->add(topologyKey("start",{regionKey}),prism.FirstShape());names->add(topologyKey("end",{regionKey}),prism.LastShape());for(const auto& [key,edges]:found->boundaryNames.images)for(const auto& edge:edges)for(TopTools_ListIteratorOfListOfShape it(prism.Generated(edge));it.More();it.Next())if(it.Value().ShapeType()==TopAbs_FACE)names->add(topologyKey("side",{featureId,key}),it.Value());}tools.push_back(prism.Shape());
    }
    return tools;
}
}
TopoDS_Shape makeExtrusions(const Sketch& sketch,const RegionReferences& references,double length,Direction direction,const TopoDS_Shape& base,TopologyNames* names,const std::string& featureId)
{
    try {
        positiveLength(length,"Extrusion length");TopologyNames generated;const auto prisms=regionPrisms(sketch,references,length,direction,names?&generated:nullptr,featureId);
        TopTools_ListOfShape arguments,tools;
        if(!base.IsNull()){requireSolids(base,"The current part must contain valid solids.");BRepBuilderAPI_Copy copy(base,Standard_True,Standard_False);arguments.Append(copy.Shape());if(names)*names=propagateTopology(*names,copy,copy.Shape());}
        else arguments.Append(prisms.front());
        for(std::size_t i=base.IsNull()?1:0;i<prisms.size();++i)tools.Append(prisms[i]);
        if(names)for(const auto& [key,list]:generated.images)for(const auto& shape:list)names->add(key,shape);
        if(tools.IsEmpty()){if(names)names->complete(prisms.front());return prisms.front();}
        BRepAlgoAPI_Fuse operation;operation.SetArguments(arguments);operation.SetTools(tools);operation.SetNonDestructive(Standard_True);operation.Build();
        if(!operation.IsDone() || operation.HasErrors())fail("Cannot combine the selected region extrusions.");
        operation.SimplifyResult();const auto result=operation.Shape();
        requireSolids(result,"Selected extrusions did not produce valid solid bodies.");if(names){*names=propagateTopology(*names,operation,result);names->complete(result);}return result;
    }catch(const Standard_Failure& e){fail(std::string("Cannot extrude selected regions: ")+e.GetMessageString());}
}
TopoDS_Shape makeCuts(const TopoDS_Shape& base,const Sketch& sketch,const RegionReferences& references,double depth,bool throughAll,Direction direction,TopologyNames* names,const std::string& featureId)
{
    try {
        requireSolids(base,"Cut requires existing valid solids.");positiveLength(depth,"Cut depth");
        BRepBuilderAPI_Copy copy(base,Standard_True,Standard_False);const auto privateBase=copy.Shape();if(names)*names=propagateTopology(*names,copy,privateBase);
        TopologyNames generated;const auto prisms=regionPrisms(sketch,references,throughAll?throughLength(privateBase,sketch,direction):depth,direction,names?&generated:nullptr,featureId);if(names)for(const auto& [key,list]:generated.images)for(const auto& shape:list)names->add(key,shape);
        TopTools_ListOfShape arguments,tools;arguments.Append(privateBase);for(const auto& prism:prisms)tools.Append(prism);
        BRepAlgoAPI_Cut operation;operation.SetArguments(arguments);operation.SetTools(tools);operation.SetNonDestructive(Standard_True);operation.Build();
        if(!operation.IsDone() || operation.HasErrors())fail("Cannot cut the selected sketch regions.");
        operation.SimplifyResult();const auto result=operation.Shape();requireSolids(result,"The cut must leave at least one valid solid body.");
        if(volume(privateBase)-volume(result)<=std::max(volume(privateBase)*1e-10,1e-15))fail("Selected cuts do not remove material. Check depth, direction and sketch location.");
        if(names){*names=propagateTopology(*names,operation,result);names->complete(result);}return result;
    }catch(const Standard_Failure& e){fail(std::string("Cannot cut selected regions: ")+e.GetMessageString());}
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
    if(std::any_of(entityIds.begin(),entityIds.end(),isFragmentReference))
        fail("A subdivided region must use the region extrusion/cut API; its arcs are not independent sketch entities.");
    for (const auto& contour : closedContours(sketch)) {
        const std::set<std::string> candidate(contour.entityIds.begin(), contour.entityIds.end());
        if (candidate != requested) {
            continue;
        }
        Sketch result;
        result.plane = sketch.plane;
        result.attachment = sketch.attachment;
        result.referencePlaneId=sketch.referencePlaneId;result.referencePlaneFrame=sketch.referencePlaneFrame;
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
        const double length = throughAll ? throughLength(privateBase, sketch, direction) : depth;
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
        if (relation.kind == RelationKind::Coincident || relation.kind==RelationKind::Concentric || relation.kind == RelationKind::Vertical)
            x[root(x, a)] = root(x, b);
        if (relation.kind == RelationKind::Coincident || relation.kind==RelationKind::Concentric || relation.kind == RelationKind::Horizontal)
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
    if (reference.role == PointRole::Midpoint && found->kind == EntityKind::Line)return {(found->first.x+found->second.x)/2,(found->first.y+found->second.y)/2};
    if(found->kind==EntityKind::Point && reference.role==PointRole::First)return found->first;
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
            if(entity.kind!=EntityKind::Point)points.push_back({{entity.id, PointRole::Second}, entity.second});
        }
    }
    return points;
}

namespace {
const SketchEntity& referencedEntity(const Sketch& sketch,const SketchPointRef& ref) {
    const auto found=std::find_if(sketch.entities.begin(),sketch.entities.end(),[&](const auto& e){return e.id==ref.entityId;});
    if(ref.entityId.empty() || found==sketch.entities.end())fail("Constraint refers to a missing entity.");return *found;
}
void validDimension(const Sketch& sketch,const SketchDimension& d) {
    finiteCoordinate(d.value);finiteCoordinate(d.annotation.x);finiteCoordinate(d.annotation.y);
    if(d.placement){finiteCoordinate(d.placement->x);finiteCoordinate(d.placement->y);}
    if(d.sense!=1 && d.sense!=-1)fail("Dimension direction must be +1 or -1.");
    const auto a=sketchPointValue(sketch,d.first);(void)a;
    if(d.kind==DimensionKind::CircleRadius || d.kind==DimensionKind::CircleDiameter) {
        if(d.first.role!=PointRole::Center || referencedEntity(sketch,d.first).kind!=EntityKind::Circle || d.second.role!=PointRole::Origin || !d.second.entityId.empty())fail("Radius/diameter must reference one circle center.");
        positiveLength(d.value,"Radius/diameter");
    } else {
        (void)sketchPointValue(sketch,d.second);
        if(pointKey(d.first)==pointKey(d.second))fail("A distance needs two distinct point references.");
        if(d.kind==DimensionKind::LineDistance || d.kind==DimensionKind::LineAngle) {
            const auto& first=referencedEntity(sketch,d.first);const auto& second=referencedEntity(sketch,d.second);
            if(first.kind!=EntityKind::Line || second.kind!=EntityKind::Line || first.id==second.id ||
               (d.first.role!=PointRole::First && d.first.role!=PointRole::Second) || (d.second.role!=PointRole::First && d.second.role!=PointRole::Second))fail("A two-line dimension requires two distinct line references.");
            if(d.kind==DimensionKind::LineAngle){if(d.value<=1e-6 || d.value>=180-1e-6)fail("Angular dimension must be between 0 and 180 degrees.");}
            else positiveLength(d.value,"Line spacing");
        } else if(d.kind==DimensionKind::PointDistance)positiveLength(d.value,"Point distance");
        else if(d.kind==DimensionKind::LineLength) {
            if(d.first.entityId!=d.second.entityId || d.first.role!=PointRole::First || d.second.role!=PointRole::Second || referencedEntity(sketch,d.first).kind!=EntityKind::Line)fail("Line length must reference the endpoints of one line.");
            positiveLength(d.value,"Line length");
        } else if(d.kind==DimensionKind::HorizontalDistance || d.kind==DimensionKind::VerticalDistance) {
            if(d.value<0)fail("Horizontal/vertical distances must be nonnegative.");
        } else fail("Unsupported dimension type.");
    }
}
}
double dimensionMeasurement(const Sketch& sketch,const SketchDimension& d) {
    if(d.kind==DimensionKind::LineDistance || d.kind==DimensionKind::LineAngle) {
        const auto& a=referencedEntity(sketch,d.first);const auto& b=referencedEntity(sketch,d.second);
        if(d.kind==DimensionKind::LineDistance)return std::abs(cross(a.first,a.second,b.first))/distance(a.first,a.second);
        Point2 u{a.second.x-a.first.x,a.second.y-a.first.y},v{b.second.x-b.first.x,b.second.y-b.first.y};
        if(d.first.role==PointRole::Second){u.x=-u.x;u.y=-u.y;}if(d.second.role==PointRole::Second){v.x=-v.x;v.y=-v.y;}
        return std::abs(std::atan2(u.x*v.y-u.y*v.x,u.x*v.x+u.y*v.y))*180.0/3.14159265358979323846;
    }
    if(d.kind==DimensionKind::CircleRadius || d.kind==DimensionKind::CircleDiameter) {
        const auto& e=referencedEntity(sketch,d.first);return e.radius*(d.kind==DimensionKind::CircleDiameter?2:1);
    }
    const auto a=sketchPointValue(sketch,d.first),b=sketchPointValue(sketch,d.second);
    if(d.kind==DimensionKind::HorizontalDistance)return std::abs(b.x-a.x);
    if(d.kind==DimensionKind::VerticalDistance)return std::abs(b.y-a.y);
    return std::hypot(b.x-a.x,b.y-a.y);
}
void validateSketchConstraintReferences(const Sketch& sketch)
{
    validateRelationGroups(sketch);
    if(sketch.relations.size()>4000 || sketch.dimensions.size()>4000)fail("Sketch exceeds the prototype constraint limit.");
    (void)sketchPoints(sketch);validatePolygons(sketch,false);std::set<std::string> ids;
    for(const auto& relation:sketch.relations) {
        if(relation.first.role==PointRole::Midpoint || relation.second.role==PointRole::Midpoint)fail("Midpoint snaps must use a Midpoint relation with real point references.");
        if(!stableIdentifier(relation.id) || !ids.insert(relation.id).second)fail("Constraint IDs must be valid and unique.");
        (void)sketchPointValue(sketch,relation.first);(void)sketchPointValue(sketch,relation.second);
        if(isNewRelation(relation.kind)){validateNewRelation(sketch,relation,false);continue;}
        if(relation.kind==RelationKind::FixedPoint) {
            if(relation.first.role==PointRole::Origin || relation.second.role!=PointRole::Origin || !relation.second.entityId.empty())fail("Fixed point must reference a sketch point and a stored anchor.");
            finiteCoordinate(relation.fixedPosition.x);finiteCoordinate(relation.fixedPosition.y);continue;
        }
        if(pointKey(relation.first)==pointKey(relation.second))fail("A relation requires distinct references.");
        if(relation.kind==RelationKind::Coincident)continue;
        const auto& a=referencedEntity(sketch,relation.first);const auto& b=referencedEntity(sketch,relation.second);
        if(relation.kind==RelationKind::Horizontal || relation.kind==RelationKind::Vertical) {
            if(!(a.kind==EntityKind::Point && b.kind==EntityKind::Point && a.id!=b.id) && (a.id!=b.id || a.kind!=EntityKind::Line || !((relation.first.role==PointRole::First&&relation.second.role==PointRole::Second)||(relation.first.role==PointRole::Second&&relation.second.role==PointRole::First))))fail("Horizontal/vertical relations require a line or distinct sketch points.");
        } else if(relation.kind==RelationKind::Parallel || relation.kind==RelationKind::Perpendicular) {
            if(a.kind!=EntityKind::Line || b.kind!=EntityKind::Line || a.id==b.id || relation.first.role!=PointRole::First || relation.second.role!=PointRole::First)fail("Parallel/perpendicular relations require two distinct line references.");
        } else if(relation.kind==RelationKind::PointOnLine || relation.kind==RelationKind::PointOnCircle) {
            if(a.id==b.id || (relation.kind==RelationKind::PointOnLine?b.kind!=EntityKind::Line:b.kind!=EntityKind::Circle))fail("Invalid point-on-geometry relation.");
        } else if(relation.kind==RelationKind::Tangent) {
            if(a.id==b.id || (a.kind!=EntityKind::Line&&a.kind!=EntityKind::Circle) || (b.kind!=EntityKind::Line&&b.kind!=EntityKind::Circle) || (a.kind==EntityKind::Line&&b.kind==EntityKind::Line))fail("Tangent supports line-circle and external circle-circle relations.");
        } else fail("Unsupported sketch relation type.");
    }
    for(const auto& d:sketch.dimensions){if(!stableIdentifier(d.id)||!ids.insert(d.id).second)fail("Dimension IDs must be valid and unique across all constraints.");if(d.first.role==PointRole::Midpoint || d.second.role==PointRole::Midpoint)fail("Dimensions require an actual sketch point; place a point at the midpoint first.");validDimension(sketch,d);}
}
Sketch canonicalizeSketchEqualities(const Sketch& sketch,const std::vector<std::pair<SketchPointRef,Point2>>& extraAnchors)
{
    validateSketchConstraintReferences(sketch);EqualityGroups groups(sketch);
    for(const auto& relation:sketch.relations)if(relation.kind==RelationKind::Coincident||relation.kind==RelationKind::Concentric||relation.kind==RelationKind::Horizontal||relation.kind==RelationKind::Vertical)groups.join(relation);
    auto result=sketch;std::map<std::size_t,double> x,y;
    const auto anchor=[&](const SketchPointRef& ref,Point2 point){const auto index=groups.index(ref);x[EqualityGroups::root(groups.x,index)]=point.x;y[EqualityGroups::root(groups.y,index)]=point.y;};
    anchor({},{});for(const auto& r:sketch.relations)if(r.kind==RelationKind::FixedPoint)anchor(r.first,r.fixedPosition);
    for(const auto& r:sketch.relations)if(r.kind==RelationKind::FixedEntity){anchor(r.first,r.fixedPosition);const auto& e=referencedEntity(sketch,r.first);if(e.kind==EntityKind::Line)anchor({e.id,PointRole::Second},r.fixedSecond);}
    for(const auto& a:extraAnchors)anchor(a.first,a.second);
    for(std::size_t i=1;i<groups.points.size();++i){const auto px=EqualityGroups::root(groups.x,i),py=EqualityGroups::root(groups.y,i);const auto point=groups.points[i].second;if(!x.count(px))x[px]=point.x;if(!y.count(py))y[py]=point.y;}
    for(std::size_t i=1;i<groups.points.size();++i)setPoint(result,groups.points[i].first,{x[EqualityGroups::root(groups.x,i)],y[EqualityGroups::root(groups.y,i)]});
    return result;
}
void validateSketchRelations(const Sketch& sketch)
{
    validateSketchConstraintReferences(sketch);validatePolygons(sketch,true);
    EqualityGroups groups(sketch);
    std::set<std::string> ids;
    std::set<std::tuple<int, PointKey, PointKey>> signatures;
    for (const auto& relation : sketch.relations) {
        if (!stableIdentifier(relation.id) || !ids.insert(relation.id).second) fail("Sketch relation IDs must be valid and unique.");
        const auto first = sketchPointValue(sketch, relation.first), second = sketchPointValue(sketch, relation.second);
        if(isNewRelation(relation.kind)){
            validateNewRelation(sketch,relation,true);
            if(relation.kind==RelationKind::Concentric)groups.join(relation);
            continue;
        }
        if (relation.kind == RelationKind::Horizontal || relation.kind == RelationKind::Vertical) {
            const auto& a=referencedEntity(sketch,relation.first);const auto& b=referencedEntity(sketch,relation.second);
            if(!(a.kind==EntityKind::Point && b.kind==EntityKind::Point) && (a.id!=b.id || a.kind!=EntityKind::Line))fail("Horizontal/vertical requires a line or points.");
        } else if(relation.kind==RelationKind::FixedPoint) {
            if(first.x!=relation.fixedPosition.x || first.y!=relation.fixedPosition.y)fail("A fixed sketch point moved from its anchor.");continue;
        } else if(relation.kind==RelationKind::Parallel || relation.kind==RelationKind::Perpendicular) {
            const auto& a=referencedEntity(sketch,relation.first);const auto& b=referencedEntity(sketch,relation.second);
            const Point2 u{a.second.x-a.first.x,a.second.y-a.first.y},v{b.second.x-b.first.x,b.second.y-b.first.y};
            const double residual=relation.kind==RelationKind::Parallel?u.x*v.y-u.y*v.x:u.x*v.x+u.y*v.y;
            if(std::abs(residual)>1e-7*std::max(1.0,std::hypot(u.x,u.y)*std::hypot(v.x,v.y)))fail("Line relation is not satisfied.");continue;
        } else if(relation.kind==RelationKind::PointOnLine || relation.kind==RelationKind::PointOnCircle) {
            const auto& e=referencedEntity(sketch,relation.second);const double error=relation.kind==RelationKind::PointOnLine?std::abs(cross(e.first,e.second,first))/distance(e.first,e.second):std::abs(distance(first,e.first)-e.radius);
            if(!std::isfinite(error)||error>1e-6)fail("Point is no longer on its referenced geometry.");continue;
        } else if(relation.kind==RelationKind::Tangent) {
            const auto& a=referencedEntity(sketch,relation.first);const auto& b=referencedEntity(sketch,relation.second);double residual;
            if(a.kind==EntityKind::Circle && b.kind==EntityKind::Circle)residual=distance(a.first,b.first)-a.radius-b.radius;
            else {const auto& line=a.kind==EntityKind::Line?a:b;const auto& circle=a.kind==EntityKind::Circle?a:b;residual=std::abs(cross(line.first,line.second,circle.first))/distance(line.first,line.second)-circle.radius;}
            if(!std::isfinite(residual)||std::abs(residual)>1e-6)fail("Tangent relation is not satisfied.");continue;
        } else if (relation.kind != RelationKind::Coincident) fail("Unsupported sketch relation type.");
        auto a = pointKey(relation.first), b = pointKey(relation.second);
        if (b < a) std::swap(a, b);
        if (!signatures.emplace(static_cast<int>(relation.kind), a, b).second) fail("Duplicate sketch relation.");
        groups.join(relation);
        if ((relation.kind == RelationKind::Coincident || relation.kind==RelationKind::Concentric || relation.kind == RelationKind::Vertical) && first.x != second.x)
            fail("Sketch relation is not satisfied: connected X coordinates must be identical.");
        if ((relation.kind == RelationKind::Coincident || relation.kind==RelationKind::Concentric || relation.kind == RelationKind::Horizontal) && first.y != second.y)
            fail("Sketch relation is not satisfied: connected Y coordinates must be identical.");
    }
    for(const auto& d:sketch.dimensions)if(d.driving) {
        double actual=dimensionMeasurement(sketch,d),expected=d.value;
        if(d.kind==DimensionKind::LineDistance) {
            const auto& a=referencedEntity(sketch,d.first);const auto& b=referencedEntity(sketch,d.second);
            const Point2 u{a.second.x-a.first.x,a.second.y-a.first.y},v{b.second.x-b.first.x,b.second.y-b.first.y};
            if(std::abs(u.x*v.y-u.y*v.x)>1e-7*std::hypot(u.x,u.y)*std::hypot(v.x,v.y))fail("Line spacing requires parallel lines; add or preserve a parallel relation.");
            actual=cross(a.first,a.second,b.first)/distance(a.first,a.second);expected*=d.sense;
        } else if(d.kind==DimensionKind::LineAngle) {
            const auto& a=referencedEntity(sketch,d.first);const auto& b=referencedEntity(sketch,d.second);
            Point2 u{a.second.x-a.first.x,a.second.y-a.first.y},v{b.second.x-b.first.x,b.second.y-b.first.y};
            if(d.first.role==PointRole::Second){u.x=-u.x;u.y=-u.y;}if(d.second.role==PointRole::Second){v.x=-v.x;v.y=-v.y;}
            actual=std::atan2(u.x*v.y-u.y*v.x,u.x*v.x+u.y*v.y)*180.0/3.14159265358979323846;expected*=d.sense;
        } else if(d.kind==DimensionKind::HorizontalDistance || d.kind==DimensionKind::VerticalDistance) {
            const auto a=sketchPointValue(sketch,d.first),b=sketchPointValue(sketch,d.second);actual=d.kind==DimensionKind::HorizontalDistance?b.x-a.x:b.y-a.y;expected*=d.sense;
        }
        if(!std::isfinite(actual)||std::abs(actual-expected)>1e-6)fail("Driving dimension is not satisfied: "+d.id);
    }
}

Sketch editSketchEntity(const Sketch& sketch, const SketchEntity& replacement)
{
    validate(sketch);
    auto candidate = sketch;
    const auto old = std::find_if(sketch.entities.begin(), sketch.entities.end(), [&](const SketchEntity& entity) { return entity.id == replacement.id; });
    if (replacement.id.empty() || old == sketch.entities.end()) fail("Cannot edit a sketch entity without its existing stable ID.");
    if (old->kind != replacement.kind) fail("Editing cannot change a sketch entity type.");
    const bool solverConstraints=!sketch.polygons.empty() || !sketch.dimensions.empty() || std::any_of(sketch.relations.begin(),sketch.relations.end(),[](const auto& r){return int(r.kind)>=int(RelationKind::FixedPoint);});
    if(solverConstraints) {
        SolveOptions options;
        const auto target=[&](PointRole role,Point2 before,Point2 after){if(before.x!=after.x || before.y!=after.y)options.pointTargets.push_back({{replacement.id,role},after,true});};
        target(old->kind==EntityKind::Circle?PointRole::Center:PointRole::First,old->first,replacement.first);
        if(old->kind==EntityKind::Circle){if(old->radius!=replacement.radius)options.radiusTargets.push_back({replacement.id,replacement.radius});}
        else if(old->kind!=EntityKind::Point)target(PointRole::Second,old->second,replacement.second);
        auto solved=requireSolvedSketch(solvePlanarSketch(sketch,options));
        for(auto& e:solved.entities)if(e.id==replacement.id)e.construction=replacement.construction;return solved;
    }
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
    for(const auto& relation:sketch.relations) if(relation.kind==RelationKind::Coincident || relation.kind==RelationKind::Concentric) groups.join(relation);
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
        if(relation.kind==RelationKind::Coincident || relation.kind==RelationKind::Concentric) groups.join(relation);
    return EqualityGroups::root(groups.x,groups.index(first)) == EqualityGroups::root(groups.x,groups.index(second));
}

Sketch moveSketchPoint(const Sketch& sketch, const SketchPointRef& reference, Point2 position,
                      const std::optional<SketchPointRef>& target, bool preserveCircleRadius)
{
    validate(sketch);
    if(reference.role==PointRole::Origin) fail("The sketch origin is fixed and cannot be dragged.");
    (void)sketchPointValue(sketch,reference);
    if(target) {
        if(pointKey(reference)==pointKey(*target)) fail("A point cannot be joined to itself.");
        position=sketchPointValue(sketch,*target);
    }
    const bool solverConstraints=!sketch.polygons.empty() || !sketch.dimensions.empty() || std::any_of(sketch.relations.begin(),sketch.relations.end(),[](const auto& r){return int(r.kind)>=int(RelationKind::FixedPoint);});
    const bool midpointTarget=target && target->role==PointRole::Midpoint;
    if(reference.role==PointRole::Midpoint)fail("Drag an endpoint or attached point to change a line midpoint.");
    if(solverConstraints || preserveCircleRadius || midpointTarget) {
        auto candidate=sketch;
        const bool existingMidpoint=midpointTarget && std::any_of(sketch.relations.begin(),sketch.relations.end(),[&](const auto& r){return r.kind==RelationKind::Midpoint && r.second.entityId==target->entityId && areSketchPointsCoincident(sketch,reference,r.first);});
        if(target && (midpointTarget?!existingMidpoint:!areSketchPointsCoincident(sketch,reference,*target))) {
            std::set<std::string> ids;for(const auto& r:sketch.relations)ids.insert(r.id);for(const auto& d:sketch.dimensions)ids.insert(d.id);
            int n=1;while(ids.count("connection-"+std::to_string(n)))++n;
            const SketchPointRef destination=midpointTarget?SketchPointRef{target->entityId,PointRole::First}:*target;
            candidate.relations.push_back({"connection-"+std::to_string(n),midpointTarget?RelationKind::Midpoint:RelationKind::Coincident,reference,destination});
        }
        SolveOptions options;options.pointTargets.push_back({reference,position,false});
        const auto& entity=referencedEntity(sketch,reference);
        if(preserveCircleRadius && entity.kind==EntityKind::Circle){
            const bool driven=std::any_of(candidate.dimensions.begin(),candidate.dimensions.end(),[&](const auto& d){return d.driving && d.first.entityId==entity.id && (d.kind==DimensionKind::CircleRadius || d.kind==DimensionKind::CircleDiameter);}) || std::any_of(candidate.relations.begin(),candidate.relations.end(),[&](const auto& r){return r.kind==RelationKind::FixedEntity && r.first.entityId==entity.id;});
            if(!driven)options.radiusTargets.push_back({entity.id,entity.radius});
            auto result=solvePlanarSketch(candidate,options);
            // Indirect dimensional relations can already determine the radius.
            // Retry without only our temporary anchor, then verify size exactly;
            // no persisted relation or driving dimension is removed.
            if(!result.success && !options.radiusTargets.empty()){
                options.radiusTargets.clear();result=solvePlanarSketch(candidate,options);
            }
            auto moved=requireSolvedSketch(result);
            if(std::abs(referencedEntity(moved,reference).radius-entity.radius)>1e-7)fail("Moving the circle conflicts with preserving its radius.");
            return moved;
        }
        return requireSolvedSketch(solvePlanarSketch(candidate,options));
    }
    auto entity=*std::find_if(sketch.entities.begin(),sketch.entities.end(),[&](const SketchEntity& e){return e.id==reference.entityId;});
    // Mouse motion is a request along the remaining degrees of freedom. A
    // rectangle corner sharing the origin's X or Y must still resize the free
    // coordinate, even when the cursor leaves that axis by a few pixels.
    EqualityGroups groups(sketch);for(const auto& relation:sketch.relations)groups.join(relation);
    const auto index=groups.index(reference);
    const bool fixedX=EqualityGroups::root(groups.x,index)==EqualityGroups::root(groups.x,0);
    const bool fixedY=EqualityGroups::root(groups.y,index)==EqualityGroups::root(groups.y,0);
    if(fixedX && fixedY)fail("This sketch point is fixed to the origin.");
    if(fixedX)position.x=0;if(fixedY)position.y=0;
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
    candidate.polygons.erase(std::remove_if(candidate.polygons.begin(),candidate.polygons.end(),[&](const auto& g){return g.centerId==entityId || std::find(g.edges.begin(),g.edges.end(),entityId)!=g.edges.end();}),candidate.polygons.end());
    candidate.relations.erase(std::remove_if(candidate.relations.begin(), candidate.relations.end(), [&](const SketchRelation& relation) {
        return relation.first.entityId == entityId || relation.second.entityId == entityId;
    }), candidate.relations.end());
    candidate.dimensions.erase(std::remove_if(candidate.dimensions.begin(),candidate.dimensions.end(),[&](const SketchDimension& dimension) {
        return dimension.first.entityId==entityId || dimension.second.entityId==entityId;
    }),candidate.dimensions.end());
    validate(candidate);
    return candidate;
}

} // namespace thecad::modeling
