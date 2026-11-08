#include "modeling/SketchArcs.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <cmath>
#include <stdexcept>

namespace thecad::modeling
{
namespace
{
constexpr double turn = 6.28318530717958647692;
Point2 subtract(Point2 a, Point2 b) { return {a.x - b.x, a.y - b.y}; }
double cross(Point2 a, Point2 b) { return a.x * b.y - a.y * b.x; }
double angle(Point2 p, Point2 center) { return std::atan2(p.y - center.y, p.x - center.x); }
double positiveAngle(double value)
{
    value = std::fmod(value, turn);
    return value < 0 ? value + turn : value;
}
SketchEntity makeArc(Point2 a, Point2 b, Point2 center, double radius, bool clockwise,
                     const std::string& id)
{
    return {EntityKind::Arc, a, b, radius, id, false, center, clockwise};
}
}

ArcAngles arcAngles(const SketchEntity& arc)
{
    const double start = angle(arc.first, arc.center);
    const double end = angle(arc.second, arc.center);
    return {start, arc.clockwise ? -positiveAngle(start - end) : positiveAngle(end - start)};
}

Point2 arcPoint(const SketchEntity& arc, double fraction)
{
    const auto angles = arcAngles(arc);
    const double theta = angles.start + fraction * angles.sweep;
    return {arc.center.x + arc.radius * std::cos(theta), arc.center.y + arc.radius * std::sin(theta)};
}

Point2 closestArcPoint(const SketchEntity& arc, Point2 target)
{
    const auto angles = arcAngles(arc);
    const double delta = angle(target, arc.center) - angles.start;
    const double along = positiveAngle(arc.clockwise ? -delta : delta);
    if (along <= std::abs(angles.sweep) && angles.sweep != 0)
        return arcPoint(arc, along / std::abs(angles.sweep));
    const auto a = subtract(target, arc.first), b = subtract(target, arc.second);
    return std::hypot(a.x, a.y) <= std::hypot(b.x, b.y) ? arc.first : arc.second;
}

TopoDS_Edge sketchArcEdge(const Sketch& sketch, const SketchEntity& arc)
{
    GC_MakeArcOfCircle curve(sketchPoint(sketch, arc.first), sketchPoint(sketch, arcPoint(arc, 0.5)),
                             sketchPoint(sketch, arc.second));
    if (!curve.IsDone())
        throw std::runtime_error("The sketch arc could not be constructed.");
    BRepBuilderAPI_MakeEdge edge(curve.Value());
    if (!edge.IsDone())
        throw std::runtime_error("The sketch arc edge could not be constructed.");
    return edge.Edge();
}

SketchEntity threePointArc(Point2 first, Point2 second, Point2 through, const std::string& id)
{
    const auto u = subtract(second, first), v = subtract(through, first);
    const double determinant = cross(u, v);
    const double length = std::hypot(u.x, u.y);
    if (length <= 1e-7 || std::abs(determinant) <= 1e-7 * length)
        throw std::runtime_error("Choose a third point away from the arc chord.");
    const double uu = u.x * u.x + u.y * u.y, vv = v.x * v.x + v.y * v.y;
    const Point2 local{(uu * v.y - vv * u.y) / (2 * determinant),
                       (vv * u.x - uu * v.x) / (2 * determinant)};
    const double radius = std::hypot(local.x, local.y);
    if (!std::isfinite(radius) || radius > 1e9)
        throw std::runtime_error("The arc radius is out of range.");
    return makeArc(first, second, {first.x + local.x, first.y + local.y}, radius, determinant > 0, id);
}

std::array<SketchEntity, 4> straightSlot(Point2 first, Point2 second, Point2 widthPick,
                                        const std::string& id)
{
    const auto axis = subtract(second, first);
    const double length = std::hypot(axis.x, axis.y);
    if (length <= 1e-7)
        throw std::runtime_error("Choose two distinct slot centers.");
    const double radius = std::abs(cross(axis, subtract(widthPick, first))) / length;
    if (radius <= 1e-7)
        throw std::runtime_error("Choose a width away from the slot centerline.");
    const Point2 normal{-axis.y / length * radius, axis.x / length * radius};
    const Point2 a{first.x + normal.x, first.y + normal.y}, b{second.x + normal.x, second.y + normal.y};
    const Point2 c{second.x - normal.x, second.y - normal.y}, d{first.x - normal.x, first.y - normal.y};
    return {{{EntityKind::Line, a, b, 1, id + "-side-1"}, makeArc(b, c, second, radius, true, id + "-cap-2"),
             {EntityKind::Line, c, d, 1, id + "-side-2"}, makeArc(d, a, first, radius, true, id + "-cap-1")}};
}

void addStraightSlot(Sketch& sketch, const std::array<SketchEntity, 4>& outline, const std::string& id)
{
    sketch.entities.insert(sketch.entities.end(), outline.begin(), outline.end());
    for (std::size_t i = 0; i < outline.size(); ++i)
    {
        const auto& a = outline[i];
        const auto& b = outline[(i + 1) % outline.size()];
        sketch.relations.push_back({id + "-join-" + std::to_string(i), RelationKind::Coincident,
                                    {a.id, PointRole::Second}, {b.id, PointRole::First}});
        sketch.relations.push_back({id + "-tangent-" + std::to_string(i), RelationKind::Tangent,
                                    {a.id, isCircular(a.kind) ? PointRole::Center : PointRole::First},
                                    {b.id, isCircular(b.kind) ? PointRole::Center : PointRole::First}});
    }
    sketch.relations.push_back({id + "-width", RelationKind::Equal, {outline[1].id, PointRole::Center},
                                {outline[3].id, PointRole::Center}});
}
}
