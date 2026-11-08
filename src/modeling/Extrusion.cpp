#include "modeling/Extrusion.h"
#include "modeling/SketchArcs.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepFilletAPI_MakeFillet2d.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <Geom_OffsetSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_Surface.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <gp_Lin.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <gp_Vec2d.hxx>
#include <TopLoc_Location.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace thecad::modeling {
namespace {
constexpr double tolerance = 1e-6;
constexpr double pi = 3.14159265358979323846;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

bool hasSolid(const TopoDS_Shape& shape)
{
    return !shape.IsNull() && TopExp_Explorer(shape, TopAbs_SOLID).More();
}

double volume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return std::abs(properties.Mass());
}

double area(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(shape, properties);
    return properties.Mass();
}

TopoDS_Shape compound(const std::vector<TopoDS_Shape>& shapes)
{
    BRep_Builder builder;
    TopoDS_Compound result;
    builder.MakeCompound(result);
    for (const auto& shape : shapes)
        if (!shape.IsNull()) builder.Add(result, shape);
    return result;
}

void buildBoolean(BRepAlgoAPI_BuilderAlgo& operation, TopologyNames* names = nullptr)
{
    operation.SetNonDestructive(true);
    operation.Build();
    require(operation.IsDone() && !operation.HasErrors(), "The extrusion boolean operation failed.");
    if (names) {
        operation.SimplifyResult();
        *names = propagateTopology(*names, operation, operation.Shape());
    }
}

template<class Operation>
TopoDS_Shape booleanShape(const TopoDS_Shape& first, const TopoDS_Shape& second,
                          TopologyNames* names = nullptr)
{
    Operation operation;
    TopTools_ListOfShape arguments, tools;
    arguments.Append(first);
    tools.Append(second);
    operation.SetArguments(arguments);
    operation.SetTools(tools);
    buildBoolean(operation, names);
    return operation.Shape();
}

TopoDS_Shape fuse(const std::vector<TopoDS_Shape>& shapes, TopologyNames* names = nullptr)
{
    require(!shapes.empty(), "No extrusion tool was generated.");
    if (shapes.size() == 1) return shapes.front();
    BRepAlgoAPI_Fuse operation;
    TopTools_ListOfShape arguments, tools;
    arguments.Append(shapes.front());
    for (std::size_t i = 1; i < shapes.size(); ++i) tools.Append(shapes[i]);
    operation.SetArguments(arguments);
    operation.SetTools(tools);
    buildBoolean(operation, names);
    return operation.Shape();
}

std::array<gp_Pnt, 8> bounds(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    require(!box.IsVoid() && !box.IsOpen(), "The selected geometry has no finite bounds.");
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0,y0,z0,x1,y1,z1);
    return {gp_Pnt(x0,y0,z0), gp_Pnt(x0,y0,z1), gp_Pnt(x0,y1,z0), gp_Pnt(x0,y1,z1),
            gp_Pnt(x1,y0,z0), gp_Pnt(x1,y0,z1), gp_Pnt(x1,y1,z0), gp_Pnt(x1,y1,z1)};
}

gp_Pln parallelPlane(const gp_Pln& plane, const gp_Pnt& point)
{
    return gp_Pln(point, plane.Axis().Direction());
}

gp_Pln translatedPlane(gp_Pln plane, const gp_Vec& offset)
{
    plane.Translate(offset);
    return plane;
}

double hit(const gp_Pnt& point, const gp_Dir& sweep, const gp_Pln& plane)
{
    const auto normal = plane.Axis().Direction();
    const double cosine = normal.Dot(sweep);
    require(std::abs(cosine) > tolerance, "The extrusion direction is parallel to a limiting plane.");
    return gp_Vec(point, plane.Location()).Dot(gp_Vec(normal)) / cosine;
}

std::pair<double, double> planeRange(const TopoDS_Shape& profile, const gp_Dir& sweep, const gp_Pln& plane)
{
    double lower = std::numeric_limits<double>::infinity();
    double upper = -lower;
    for (const auto& point : bounds(profile)) {
        const double distance = hit(point, sweep, plane);
        lower = std::min(lower, distance);
        upper = std::max(upper, distance);
    }
    return {lower, upper};
}

TopoDS_Shape planeHalf(const gp_Pln& plane, const gp_Dir& direction)
{
    BRepPrimAPI_MakeHalfSpace half(BRepBuilderAPI_MakeFace(plane).Face(),
                                  plane.Location().Translated(gp_Vec(direction)));
    require(half.IsDone(), "Cannot construct the extrusion limiting plane.");
    return half.Solid();
}

TopLoc_Location translation(const gp_Vec& offset)
{
    if (offset.SquareMagnitude() < tolerance * tolerance) return {};
    gp_Trsf transform;
    transform.SetTranslation(offset);
    return TopLoc_Location(transform);
}

void appendNames(TopologyNames& destination, const TopologyNames& source)
{
    for (const auto& [key, shapes] : source.images)
        for (const auto& shape : shapes) destination.add(key, shape);
}

TopoDS_Face planarFace(const gp_Pln& plane, const TopoDS_Wire& outer,
                       const std::vector<TopoDS_Wire>& holes = {})
{
    BRepBuilderAPI_MakeFace builder(plane, outer, true);
    for (const auto& hole : holes) builder.Add(TopoDS::Wire(hole.Reversed()));
    require(builder.IsDone() && BRepCheck_Analyzer(builder.Face()).IsValid(),
            "The thin profile overlaps itself or its thickness collapses a boundary.");
    return builder.Face();
}

TopoDS_Wire offsetWire(const TopoDS_Wire& wire, const gp_Pln& plane, double distance)
{
    if (std::abs(distance) < tolerance) return BRepTools::OuterWire(planarFace(plane, wire));
    BRepOffsetAPI_MakeOffset offset(BRepTools::OuterWire(planarFace(plane, wire)), GeomAbs_Intersection);
    offset.Perform(distance);
    require(offset.IsDone() && !offset.Shape().IsNull(), "Cannot offset this contour by the specified thickness.");
    TopExp_Explorer wires(offset.Shape(), TopAbs_WIRE);
    require(wires.More(), "The thickness collapses this contour.");
    const auto result = TopoDS::Wire(wires.Current());
    wires.Next();
    require(!wires.More(), "The thickness splits this contour into multiple loops.");
    return BRepTools::OuterWire(planarFace(plane, result));
}

std::pair<double,double> thicknesses(const ExtrudeThin& thin)
{
    require(std::isfinite(thin.thickness) && thin.thickness > tolerance &&
            std::isfinite(thin.secondThickness) && thin.secondThickness > tolerance,
            "Thin feature thickness must be positive.");
    if (thin.mode == ThinMode::MidPlane) return {thin.thickness / 2, thin.thickness / 2};
    const double other = thin.mode == ThinMode::TwoDirections ? thin.secondThickness : 0;
    return thin.reverse ? std::pair<double,double>{other, thin.thickness}
                        : std::pair<double,double>{thin.thickness, other};
}

std::vector<TopoDS_Face> thinClosed(const TopoDS_Face& face, const gp_Pln& plane, const ExtrudeThin& thin)
{
    const auto [outside, inside] = thicknesses(thin);
    const auto outer = BRepTools::OuterWire(face);
    std::vector<TopoDS_Face> walls;
    for (TopExp_Explorer wires(face, TopAbs_WIRE); wires.More(); wires.Next()) {
        const auto wire = TopoDS::Wire(wires.Current());
        const bool exterior = wire.IsSame(outer);
        const auto larger = offsetWire(wire, plane, exterior ? outside : inside);
        const auto smaller = offsetWire(wire, plane, -(exterior ? inside : outside));
        walls.push_back(planarFace(plane, larger, {smaller}));
    }
    return walls;
}

const SketchEntity* singleOpenArc(const Sketch& sketch)
{
    const SketchEntity* arc = nullptr;
    for (const auto& entity : sketch.entities) {
        if (entity.construction || entity.kind == EntityKind::Point) continue;
        if (arc || entity.kind != EntityKind::Arc) return nullptr;
        arc = &entity;
    }
    return arc;
}

TopoDS_Face thinArc(const Sketch& sketch, const gp_Pln& plane, const ExtrudeThin& thin,
                    const SketchEntity& arc)
{
    const auto [left, right] = thicknesses(thin);
    auto inner = arc, outer = arc;
    inner.radius -= arc.clockwise ? right : left;
    outer.radius += arc.clockwise ? left : right;
    require(inner.radius > tolerance, "Thin thickness collapses the arc's inner radius.");
    for (auto* offset : {&inner, &outer}) {
        const auto a = arcPoint(*offset, 0), b = arcPoint(*offset, 1);
        offset->first = a;
        offset->second = b;
    }
    BRepBuilderAPI_MakeWire wire;
    wire.Add(sketchArcEdge(sketch, outer));
    wire.Add(BRepBuilderAPI_MakeEdge(sketchPoint(sketch, outer.second), sketchPoint(sketch, inner.second)));
    wire.Add(TopoDS::Edge(sketchArcEdge(sketch, inner).Reversed()));
    wire.Add(BRepBuilderAPI_MakeEdge(sketchPoint(sketch, inner.first), sketchPoint(sketch, outer.first)));
    require(wire.IsDone(), "Cannot close the thin arc profile.");
    return planarFace(plane, wire.Wire());
}

TopoDS_Face thinOpen(const Sketch& sketch, const gp_Pln& plane, const ExtrudeThin& thin)
{
    if (const auto* arc = singleOpenArc(sketch)) return thinArc(sketch, plane, thin, *arc);
    const auto path = openSketchPath(sketch);
    const auto [left, right] = thicknesses(thin);
    std::vector<gp_Vec2d> normals;
    for (std::size_t i = 1; i < path.size(); ++i) {
        const gp_Vec2d edge(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y);
        normals.push_back(gp_Vec2d(-edge.Y(), edge.X()).Normalized());
    }
    const auto at = [&](std::size_t i, double distance) {
        gp_Vec2d normal = i == path.size() - 1 ? normals.back() : normals[i];
        if (i > 0 && i < path.size() - 1) {
            const double divisor = 1 + normal.Dot(normals[i - 1]);
            require(divisor > tolerance, "Cannot thicken a reversing segment.");
            normal = (normal + normals[i - 1]) / divisor;
        }
        return sketchPoint(sketch, {path[i].x + distance * normal.X(), path[i].y + distance * normal.Y()});
    };
    BRepBuilderAPI_MakePolygon polygon;
    for (std::size_t i = 0; i < path.size(); ++i) polygon.Add(at(i, left));
    for (std::size_t i = path.size(); i > 0; --i) polygon.Add(at(i - 1, -right));
    polygon.Close();
    require(polygon.IsDone(), "Cannot close the thin contour.");
    const auto face = planarFace(plane, polygon.Wire());
    if (thin.cornerRadius == 0) return face;
    require(std::isfinite(thin.cornerRadius) && thin.cornerRadius > 0, "Enter a positive auto-fillet radius.");
    BRepFilletAPI_MakeFillet2d fillets(face);
    for (std::size_t i = 1; i + 1 < path.size(); ++i) {
        const double turn = normals[i - 1].Crossed(normals[i]);
        if (std::abs(turn) < tolerance) continue;
        const auto leftCorner = at(i, left);
        const auto rightCorner = at(i, -right);
        const auto other = turn > 0 ? rightCorner : leftCorner;
        const auto inner = turn > 0 ? leftCorner : rightCorner;
        // Add both concentric wall corners using the vertices of the original face.
        for (const auto& corner : {inner, other}) {
            for (TopExp_Explorer vertices(face, TopAbs_VERTEX); vertices.More(); vertices.Next()) {
                const auto vertex = TopoDS::Vertex(vertices.Current());
                if (BRep_Tool::Pnt(vertex).Distance(corner) >= tolerance) continue;
                const double radius = thin.cornerRadius + (corner.IsEqual(inner, tolerance) ? 0 : left + right);
                require(!fillets.AddFillet(vertex, radius).IsNull(), "The auto-fillet radius does not fit the open contour.");
                break;
            }
        }
    }
    fillets.Build();
    require(fillets.IsDone() && BRepCheck_Analyzer(fillets.Shape()).IsValid(), "Cannot build the rounded thin profile.");
    return TopoDS::Face(fillets.Shape());
}

void addPrismNames(TopologyNames& names, BRepPrimAPI_MakePrism& prism,
                   const Contour& region, const std::string& feature, const std::string& suffix,
                   const TopLoc_Location& offset)
{
    auto ids = region.entityIds;
    ids.push_back(feature);
    const auto key = topologyKey("region", ids);
    names.add(topologyKey("start" + suffix, {key}), prism.FirstShape());
    names.add(topologyKey("end" + suffix, {key}), prism.LastShape());
    for (const auto& [source, edges] : region.boundaryNames.images)
        for (const auto& edge : edges)
            for (TopTools_ListIteratorOfListOfShape it(prism.Generated(edge.Moved(offset))); it.More(); it.Next())
                if (it.Value().ShapeType() == TopAbs_FACE)
                    names.add(topologyKey("side" + suffix, {feature, source}), it.Value());
}

TopoDS_Shape draftedPrism(const TopoDS_Face& profile, const gp_Dir& sweep, double length,
                          const ExtrudeLeg& leg, const Contour& region, const TopLoc_Location& offset,
                          TopologyNames& names, const std::string& feature, const std::string& suffix)
{
    require(length > tolerance && std::isfinite(length), "The extrusion end must lie beyond its start.");
    BRepPrimAPI_MakePrism prism(profile, gp_Vec(sweep) * length, true);
    require(prism.IsDone() && hasSolid(prism.Shape()), "Cannot extrude the selected profile.");
    addPrismNames(names, prism, region, feature, suffix, offset);
    if (leg.draftAngle == 0) return prism.Shape();
    require(std::isfinite(leg.draftAngle) && leg.draftAngle > 0 && leg.draftAngle < 90,
            "Draft angle must be between zero and 90 degrees.");
    const gp_Pln neutral = BRepAdaptor_Surface(profile).Plane();
    require(std::abs(neutral.Axis().Direction().Dot(sweep)) > 1 - tolerance,
            "Draft requires an extrusion direction perpendicular to the sketch.");
    BRepOffsetAPI_DraftAngle draft(prism.Shape());
    const double angle = (leg.draftOutward ? -1 : 1) * leg.draftAngle * pi / 180;
    for (TopExp_Explorer faces(prism.Shape(), TopAbs_FACE); faces.More(); faces.Next()) {
        const auto& face = faces.Current();
        if (face.IsSame(prism.FirstShape()) || face.IsSame(prism.LastShape())) continue;
        draft.Add(TopoDS::Face(face), sweep, angle, neutral);
        require(draft.AddDone(), "This draft angle or profile cannot be tapered.");
    }
    draft.Build();
    require(draft.IsDone(), "The draft collapses or intersects the extrusion.");
    names = propagateTopology(names, draft, draft.Shape());
    return draft.Shape();
}

TopoDS_Shape boundedTool(const TopoDS_Face& profile, const gp_Dir& sweep, const gp_Pln& start,
                         const gp_Pln& end, const ExtrudeLeg& leg, const Contour& region,
                         TopologyNames& names, const std::string& feature, const std::string& suffix)
{
    const gp_Pln sketch = BRepAdaptor_Surface(profile).Plane();
    const bool flatStart = std::abs(sketch.Axis().Direction().Dot(start.Axis().Direction())) > 1 - 1e-10;
    const bool flatEnd = std::abs(sketch.Axis().Direction().Dot(end.Axis().Direction())) > 1 - 1e-10;
    if (flatStart && flatEnd) {
        const double begin = hit(sketch.Location(), sweep, start);
        const double finish = hit(sketch.Location(), sweep, end);
        const auto offset = translation(gp_Vec(sweep) * begin);
        return draftedPrism(TopoDS::Face(profile.Moved(offset)), sweep, finish - begin,
                            leg, region, offset, names, feature, suffix);
    }
    require(leg.draftAngle == 0, "Draft requires start and end planes parallel to the sketch.");
    const auto begin = planeRange(profile, sweep, start).first;
    const auto finish = planeRange(profile, sweep, end).second;
    for (const auto& point : bounds(profile))
        require(hit(point, sweep, end) > hit(point, sweep, start) + tolerance,
                "The limiting planes must bound the complete profile in this direction.");
    const double margin = std::max(1.0, (finish - begin) * 0.01);
    const auto offset = translation(gp_Vec(sweep) * (begin - margin));
    auto tool = draftedPrism(TopoDS::Face(profile.Moved(offset)), sweep, finish - begin + 2 * margin,
                             leg, region, offset, names, feature, suffix);
    tool = booleanShape<BRepAlgoAPI_Common>(tool, planeHalf(start, sweep), &names);
    tool = booleanShape<BRepAlgoAPI_Common>(tool, planeHalf(end, sweep.Reversed()), &names);
    auto ids = region.entityIds;
    ids.push_back(feature);
    const auto key = topologyKey("region", ids);
    for (TopExp_Explorer faces(tool, TopAbs_FACE); faces.More(); faces.Next()) {
        const BRepAdaptor_Surface surface(TopoDS::Face(faces.Current()));
        if (surface.GetType() != GeomAbs_Plane) continue;
        const auto plane = surface.Plane();
        if (plane.Axis().Direction().IsParallel(start.Axis().Direction(), tolerance) && start.Distance(plane.Location()) < tolerance)
            names.add(topologyKey("start" + suffix, {key}), faces.Current());
        if (plane.Axis().Direction().IsParallel(end.Axis().Direction(), tolerance) && end.Distance(plane.Location()) < tolerance)
            names.add(topologyKey("end" + suffix, {key}), faces.Current());
    }
    return tool;
}

gp_Pln referencedPlane(const ExtrudeReference& reference, const gp_Pln& sketch, bool vertex)
{
    require(reference.datum.kind == (vertex ? DatumKind::Point : DatumKind::Plane),
            vertex ? "Select a vertex or point." : "Select a plane or planar face.");
    return vertex ? parallelPlane(sketch, reference.datum.frame.Location()) : gp_Pln(reference.datum.frame);
}

double throughDepth(const TopoDS_Shape& stock, const gp_Pln& start, const gp_Dir& sweep)
{
    require(!stock.IsNull(), "This end condition requires existing geometry.");
    double depth = 0;
    for (const auto& point : bounds(stock))
        depth = std::max(depth, -hit(point, sweep, start));
    return depth + std::max(1.0, depth * 0.01);
}

bool isCurved(const ExtrudeReference& reference)
{
    return !reference.shape.IsNull() && BRepAdaptor_Surface(TopoDS::Face(reference.shape)).GetType() != GeomAbs_Plane;
}

bool onPlane(const TopoDS_Face& face, const gp_Pln& plane)
{
    const BRepAdaptor_Surface surface(face);
    return surface.GetType() == GeomAbs_Plane &&
           surface.Plane().Axis().Direction().IsParallel(plane.Axis().Direction(), tolerance) &&
           plane.Distance(surface.Plane().Location()) < tolerance;
}

double capArea(const TopoDS_Shape& shape, const gp_Pln& plane)
{
    double result = 0;
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next())
        if (onPlane(TopoDS::Face(faces.Current()), plane)) result += area(faces.Current());
    return result;
}

TopoDS_Shape surfaceLimit(const ExtrudeReference& support, const ExtrudeLeg& leg, const gp_Dir& sweep,
                          const TopoDS_Face& profile, const gp_Pln& start)
{
    const auto face = TopoDS::Face(support.shape);
    auto surface = BRep_Tool::Surface(face);
    const BRepAdaptor_Surface adaptor(face);
    // Analytic surfaces can extend beyond the selected face; a general trimmed
    // surface must split the full tool within its actual boundaries.
    const auto type = adaptor.GetType();
    const bool extend = type == GeomAbs_Cylinder || type == GeomAbs_Cone ||
                        type == GeomAbs_Sphere || type == GeomAbs_Torus;
    const double offset = leg.end == ExtrudeEnd::SurfaceOffset ?
                          (leg.reverseOffset ? leg.offset : -leg.offset) : 0;
    if (!extend && offset == 0) return face;
    if (std::abs(offset) > tolerance) {
        if (leg.translateSurface) {
            surface = Handle(Geom_Surface)::DownCast(surface->Translated(gp_Vec(sweep) * offset));
        } else {
            GProp_GProps properties;
            BRepGProp::SurfaceProperties(profile, properties);
            auto center = properties.CentreOfMass();
            center.Translate(gp_Vec(sweep) * hit(center, sweep, start));
            IntCurvesFace_ShapeIntersector intersections;
            const auto extended = extend ? BRepBuilderAPI_MakeFace(surface, tolerance).Face() : face;
            intersections.Load(extended, tolerance);
            intersections.Perform(gp_Lin(center, sweep), tolerance, 1e12);
            require(intersections.IsDone() && intersections.NbPnt() > 0,
                    "The reference surface does not intersect the extrusion axis.");
            intersections.SortResult();
            gp_Pnt point;
            gp_Vec du, dv;
            surface->D1(intersections.UParameter(1), intersections.VParameter(1), point, du, dv);
            const gp_Vec normal = du.Crossed(dv).Normalized();
            const auto projection = normal.Dot(gp_Vec(sweep));
            require(std::abs(projection) > tolerance, "The offset surface is tangent to the extrusion direction.");
            const double normalOffset = projection > 0 ? offset : -offset;
            if (type == GeomAbs_Cylinder) {
                auto cylinder = adaptor.Cylinder();
                const auto axis = cylinder.Axis();
                const gp_Vec displacement(axis.Location(), point);
                const auto radial = displacement - gp_Vec(axis.Direction()) * displacement.Dot(gp_Vec(axis.Direction()));
                const double radius = cylinder.Radius() + normalOffset * normal.Dot(radial.Normalized());
                require(radius > tolerance, "The offset collapses the cylindrical surface.");
                cylinder.SetRadius(radius);
                surface = new Geom_CylindricalSurface(cylinder);
            } else if (type == GeomAbs_Sphere) {
                auto sphere = adaptor.Sphere();
                const double radius = sphere.Radius() + normalOffset * normal.Dot(gp_Vec(sphere.Location(), point).Normalized());
                require(radius > tolerance, "The offset collapses the spherical surface.");
                sphere.SetRadius(radius);
                surface = new Geom_SphericalSurface(sphere);
            } else {
                surface = new Geom_OffsetSurface(surface, normalOffset);
            }
        }
    }
    BRepBuilderAPI_MakeFace limitingFace(surface, tolerance);
    require(limitingFace.IsDone(), "Cannot construct the reference surface.");
    return limitingFace.Face();
}

TopoDS_Shape splitAtLimit(const TopoDS_Face& profile, const gp_Dir& sweep, const gp_Pln& start,
                          const TopoDS_Shape& splitter, const TopoDS_Shape& boundsShape,
                          const ExtrudeLeg& leg, const Contour& region, TopologyNames& names,
                          const std::string& feature, const std::string& suffix, bool reverseRoles = false)
{
    require(!splitter.IsNull() && !boundsShape.IsNull(), "Select a limiting face or body.");
    require(leg.draftAngle == 0, "Draft with a curved surface or body limit is unavailable.");
    const auto end = translatedPlane(start, gp_Vec(sweep) *
        (throughDepth(boundsShape, start, sweep) + 2 * std::abs(leg.offset)));
    const auto raw = boundedTool(profile, sweep, start, end, leg, region, names, feature, suffix);
    const auto wholeCap = capArea(raw, start);
    const auto pieces = booleanShape<BRepAlgoAPI_Splitter>(raw, splitter, &names);
    TopoDS_Shape result;
    const auto areaTolerance = std::max(tolerance * tolerance, wholeCap * 1e-8);
    for (TopExp_Explorer solids(pieces, TopAbs_SOLID); solids.More(); solids.Next()) {
        if (capArea(solids.Current(), start) < wholeCap - areaTolerance) continue;
        require(result.IsNull(), "Several pieces match the extrusion start. Choose a different limit.");
        result = solids.Current();
    }
    require(wholeCap > areaTolerance && !result.IsNull() && capArea(result, end) < areaTolerance,
            "The selected limit does not intercept the complete profile in this direction.");
    names.complete(result);
    auto ids = region.entityIds;
    ids.push_back(feature);
    const auto key = topologyKey("region", ids);
    for (TopExp_Explorer faces(result, TopAbs_FACE); faces.More(); faces.Next()) {
        if (!names.nameOf(faces.Current()).empty()) continue;
        names.add(topologyKey("end" + suffix, {key}), faces.Current());
    }
    if (reverseRoles) std::swap(names.images[topologyKey("start" + suffix, {key})],
                               names.images[topologyKey("end" + suffix, {key})]);
    return result;
}

gp_Pln legLimit(const TopoDS_Face& profile, const gp_Dir& sweep, const gp_Pln& start,
                const TopoDS_Shape& stock, const ExtrudeLeg& leg, const ExtrudeReference& target)
{
    const auto sketch = BRepAdaptor_Surface(profile).Plane();
    switch (leg.end) {
    case ExtrudeEnd::Blind:
    case ExtrudeEnd::MidPlane:
        require(std::isfinite(leg.depth) && leg.depth > tolerance && leg.depth <= 1e9,
                "Enter a positive depth no greater than 1e9 mm.");
        return translatedPlane(start, gp_Vec(sweep) * (leg.end == ExtrudeEnd::MidPlane ? leg.depth / 2 : leg.depth));
    case ExtrudeEnd::ThroughAll:
    case ExtrudeEnd::ThroughAllBoth:
        return translatedPlane(start, gp_Vec(sweep) * throughDepth(stock, start, sweep));
    case ExtrudeEnd::Vertex:
        return referencedPlane(target, sketch, true);
    case ExtrudeEnd::Surface:
        return referencedPlane(target, sketch, false);
    case ExtrudeEnd::SurfaceOffset: {
        require(std::isfinite(leg.offset) && leg.offset >= 0 && leg.offset <= 1e9,
                "Enter a nonnegative surface offset.");
        const auto plane = referencedPlane(target, sketch, false);
        gp_Dir offset = leg.translateSurface ? sweep : plane.Axis().Direction();
        if (offset.Dot(sweep) < 0) offset.Reverse();
        return translatedPlane(plane, gp_Vec(offset) * (leg.reverseOffset ? leg.offset : -leg.offset));
    }
    case ExtrudeEnd::UpToNext:
    case ExtrudeEnd::Body:
        throw std::runtime_error("This end condition requires a limiting solid.");
    }
    throw std::runtime_error("Unknown extrusion end condition.");
}

TopoDS_Face stockEnvelope(const TopoDS_Shape& stock, const gp_Ax3& axes, const gp_Dir& sweep)
{
    gp_Trsf frame;
    frame.SetTransformation(axes);
    const gp_Pln plane(axes);
    const gp_Vec direction(sweep);
    double x0 = std::numeric_limits<double>::infinity(), y0 = x0, x1 = -x0, y1 = -x0;
    for (auto point : bounds(stock)) {
        point.Translate(direction * hit(point, sweep, plane));
        point.Transform(frame);
        x0 = std::min(x0, point.X()); x1 = std::max(x1, point.X());
        y0 = std::min(y0, point.Y()); y1 = std::max(y1, point.Y());
    }
    return BRepBuilderAPI_MakeFace(plane, x0 - 1, x1 + 1, y0 - 1, y1 + 1).Face();
}
} // namespace

std::vector<std::string> openExtrusionProfile(const Sketch& sketch)
{
    if (const auto* arc = singleOpenArc(sketch)) return {arc->id};
    try {
        openSketchPath(sketch);
        std::vector<std::string> ids;
        for (const auto& entity : sketch.entities)
            if (!entity.construction && entity.kind != EntityKind::Point) ids.push_back(entity.id);
        return ids;
    } catch (const std::exception&) {
        return {};
    }
}

TopoDS_Shape extrudeProfile(const Sketch& sketch, const RegionReferences& references,
                            const TopoDS_Shape& stock, bool cut, Direction direction,
                            const ExtrudeDefinition& definition, const ExtrudeSupports& supports,
                            TopologyNames& names, const std::string& feature)
{
    require(!references.empty(), "Select at least one profile.");
    require(!cut || hasSolid(stock), "Cut-Extrude requires an existing solid.");
    require(cut || !definition.flipCut, "Flip side to cut applies to cuts only.");
    require(cut || definition.merge || definition.scope.empty(), "A separate boss cannot use a merge scope.");
    if (definition.thin) {
        const auto& thin = *definition.thin;
        require(std::isfinite(thin.endCap) && thin.endCap >= 0 && thin.endCap <= 1e9 &&
                std::isfinite(thin.cornerRadius) && thin.cornerRadius >= 0 && thin.cornerRadius <= 1e9,
                "Invalid thin feature fillet radius or cap thickness.");
    }
    const gp_Pln sketchPlane(sketchAxes(sketch));
    gp_Dir sweep = sketchPlane.Axis().Direction();
    if (definition.direction) {
        require(supports.direction.datum.kind == DatumKind::Axis, "Select a straight edge, line or axis for the direction.");
        sweep = supports.direction.datum.frame.Direction();
        if (sweep.Dot(sketchPlane.Axis().Direction()) < 0) sweep.Reverse();
    }
    if (direction == Direction::Negative) sweep.Reverse();
    require(std::abs(sweep.Dot(sketchPlane.Axis().Direction())) > tolerance,
            "The extrusion direction cannot be parallel to the sketch.");
    require((definition.start != ExtrudeStart::Vertex && definition.start != ExtrudeStart::Surface) ||
            definition.startReference.has_value(), "Select a starting reference.");
    const bool curvedStart = definition.start == ExtrudeStart::Surface && isCurved(supports.start);
    gp_Pln start = sketchPlane;
    if (definition.start == ExtrudeStart::Offset) {
        require(std::isfinite(definition.startOffset) && std::abs(definition.startOffset) <= 1e9, "Invalid start offset.");
        start.Translate(gp_Vec(sketchPlane.Axis().Direction()) * definition.startOffset);
    } else if (definition.start != ExtrudeStart::SketchPlane && !curvedStart) {
        start = referencedPlane(supports.start, sketchPlane, definition.start == ExtrudeStart::Vertex);
    }
    auto second = definition.second;
    const bool symmetric = definition.first.end == ExtrudeEnd::MidPlane || definition.first.end == ExtrudeEnd::ThroughAllBoth;
    require(!symmetric || !second, "A symmetric end condition already uses both directions.");
    if (symmetric) second = definition.first;
    if (second)
        require(symmetric || (second->end != ExtrudeEnd::MidPlane && second->end != ExtrudeEnd::ThroughAllBoth),
                "Direction 2 cannot use a symmetric end condition.");
    const auto regions = closedContours(sketch);
    const auto open = definition.thin ? openExtrusionProfile(sketch) : std::vector<std::string>{};
    std::set<std::set<std::string>> unique;
    std::vector<TopoDS_Shape> tools, envelopes;
    TopologyNames toolNames;
    for (const auto& ids : references) {
        const std::set<std::string> key(ids.begin(), ids.end());
        require(!key.empty() && key.size() == ids.size() && unique.insert(key).second,
                "The selected profiles contain duplicate or empty references.");
        const auto found = std::find_if(regions.begin(), regions.end(), [&](const auto& region) {
            return std::set<std::string>(region.entityIds.begin(), region.entityIds.end()) == key;
        });
        const bool openSelected = !open.empty() && std::set<std::string>(open.begin(), open.end()) == key;
        require(found != regions.end() || openSelected, "A selected profile changed or disappeared. Reselect it.");
        if (openSelected && cut) {
            const auto valid = [](const ExtrudeLeg& leg) {
                return leg.end == ExtrudeEnd::Blind || leg.end == ExtrudeEnd::ThroughAll ||
                       leg.end == ExtrudeEnd::ThroughAllBoth;
            };
            require(valid(definition.first) && (!second || valid(*second)),
                    "An open Thin Cut supports Blind and Through All end conditions.");
        }
        const Contour region = found != regions.end() ? *found : Contour{ids, {}, {}};
        std::vector<TopoDS_Face> profiles;
        if (openSelected) profiles.push_back(thinOpen(sketch, sketchPlane, *definition.thin));
        else if (definition.thin) profiles = thinClosed(region.face, sketchPlane, *definition.thin);
        else profiles.push_back(region.face);
        const auto addLeg = [&](const ExtrudeLeg& leg, const ExtrudeReference& support,
                                const gp_Dir& legDirection, const std::string& suffix) {
            if (leg.end == ExtrudeEnd::SurfaceOffset)
                require(std::isfinite(leg.offset) && leg.offset >= 0 && leg.offset <= 1e9, "Enter a nonnegative surface offset.");
            const bool bodyEnd = leg.end == ExtrudeEnd::Body || leg.end == ExtrudeEnd::UpToNext;
            const bool curvedEnd = (leg.end == ExtrudeEnd::Surface || leg.end == ExtrudeEnd::SurfaceOffset) && isCurved(support);
            const bool referencedEnd = bodyEnd || curvedEnd || leg.end == ExtrudeEnd::Vertex ||
                leg.end == ExtrudeEnd::Surface || leg.end == ExtrudeEnd::SurfaceOffset;
            const bool needsTarget = referencedEnd && leg.end != ExtrudeEnd::UpToNext;
            require(!needsTarget || leg.target.has_value(), "Select a limiting reference.");
            require(!curvedStart || (!curvedEnd && !bodyEnd && !symmetric && !second),
                    "A curved start requires one direction with a planar end.");
            require(!(bodyEnd || curvedEnd || curvedStart) || !definition.flipCut,
                    "Flip side to cut requires planar start and end limits.");
            const auto end = bodyEnd || curvedEnd ? start : legLimit(profiles.front(), legDirection, start, stock, leg, support);
            const auto addTool = [&](const TopoDS_Face& face, const gp_Pln& from, const gp_Pln& to,
                                     const std::string& name) {
                TopologyNames local;
                if (curvedStart) {
                    tools.push_back(splitAtLimit(face, legDirection.Reversed(), to, supports.start.shape,
                        supports.start.shape, leg, region, local, feature, name, true));
                } else if (bodyEnd) {
                    const auto& body = leg.end == ExtrudeEnd::UpToNext ? stock : support.body;
                    tools.push_back(splitAtLimit(face, legDirection, from, body, body, leg, region, local, feature, name));
                } else if (curvedEnd) {
                    const auto splitter = surfaceLimit(support, leg, legDirection, face, from);
                    tools.push_back(splitAtLimit(face, legDirection, from, splitter, support.shape,
                                                 leg, region, local, feature, name));
                } else {
                    tools.push_back(boundedTool(face, legDirection, from, to, leg, region, local, feature, name));
                }
                appendNames(toolNames, local);
            };
            for (std::size_t i = 0; i < profiles.size(); ++i) {
                const auto name = definition.thin ? suffix + "-wall" + std::to_string(i) : suffix;
                addTool(profiles[i], start, end, name);
            }
            if (cut && definition.flipCut) {
                const auto envelope = stockEnvelope(stock, sketchAxes(sketch), legDirection);
                TopologyNames ignored;
                ExtrudeLeg straight;
                envelopes.push_back(boundedTool(envelope, legDirection, start, end, straight, {}, ignored, {}, {}));
            }
            if (definition.thin && definition.thin->endCap > 0) {
                require(!cut && stock.IsNull() && !openSelected && !second && leg.end == ExtrudeEnd::Blind &&
                        leg.draftAngle == 0 && 2 * definition.thin->endCap < leg.depth,
                        "End caps require a first, closed, single-direction Blind boss with enough depth.");
                const auto cap = gp_Vec(legDirection) * definition.thin->endCap;
                addTool(region.face, start, translatedPlane(start, cap), suffix + "-start-cap");
                addTool(region.face, translatedPlane(end, -cap), end, suffix + "-end-cap");
            }
        };
        addLeg(definition.first, supports.first, sweep, "");
        if (second) addLeg(*second, supports.second, sweep.Reversed(), "-direction2");
    }
    auto tool = fuse(tools, &toolNames);
    require(hasSolid(tool) && BRepCheck_Analyzer(tool).IsValid(), "The settings do not produce a valid solid extrusion.");
    const auto scopedStock = supports.scope.empty() ? stock : compound(supports.scope);
    const auto restoreOtherBodies = [&](const TopoDS_Shape& modified, const TopologyNames& previousNames) {
        if (supports.scope.empty()) return modified;
        std::vector<TopoDS_Shape> bodies{modified};
        for (TopExp_Explorer solids(stock, TopAbs_SOLID); solids.More(); solids.Next())
            if (std::none_of(supports.scope.begin(), supports.scope.end(), [&](const auto& body) {
                return body.IsSame(solids.Current());
            })) bodies.push_back(solids.Current());
        appendNames(names, previousNames);
        return compound(bodies);
    };
    TopoDS_Shape result = tool;
    if (cut) {
        if (definition.flipCut)
            tool = booleanShape<BRepAlgoAPI_Cut>(fuse(envelopes), tool, &toolNames);
        appendNames(names, toolNames);
        const auto previousNames = names;
        result = booleanShape<BRepAlgoAPI_Cut>(scopedStock, tool, &names);
        const double before = volume(scopedStock);
        require(before - volume(result) > std::max(before * 1e-10, 1e-15),
                "The cut does not remove material from the selected bodies.");
        result = restoreOtherBodies(result, previousNames);
    } else {
        appendNames(names, toolNames);
        if (!stock.IsNull() && definition.merge) {
            const auto previousNames = names;
            result = booleanShape<BRepAlgoAPI_Fuse>(scopedStock, tool, &names);
            result = restoreOtherBodies(result, previousNames);
        } else if (!stock.IsNull()) {
            result = compound({stock, tool});
        }
    }
    require(hasSolid(result) && BRepCheck_Analyzer(result).IsValid(), "The feature must leave at least one valid solid body.");
    names.complete(result);
    return result;
}
}
