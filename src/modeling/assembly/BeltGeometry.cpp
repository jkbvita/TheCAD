#include "modeling/assembly/BeltGeometry.h"
#include <cmath>
#include <stdexcept>
namespace thecad::modeling::assembly
{
namespace
{
constexpr double pi = 3.14159265358979323846;
struct Vec
{
    double x = 0, y = 0;
    Vec operator+(Vec v) const { return {x + v.x, y + v.y}; }
    Vec operator-(Vec v) const { return {x - v.x, y - v.y}; }
    Vec operator*(double v) const { return {x * v, y * v}; }
    double norm() const { return std::hypot(x, y); }
    Point2 point() const { return {x, y}; }
};
struct Route
{
    gp_Ax3 frame;
    std::vector<Vec> centers, normals;
    std::vector<double> radii, spans;
    std::vector<int> sides;
    double length = 0;
    int winding = -1;
};
Route calculate(const std::vector<BeltPulley>& pulleys, double offset)
{
    if (pulleys.size() < 2 || pulleys.size() > 64 || !std::isfinite(offset) ||
        std::abs(offset) > 1e9)
        throw std::runtime_error(
            "Belt/Chain requires 2 to 64 ordered pulleys and a finite plane offset.");
    Route route;
    const auto n = pulleys[0].frame.Direction();
    auto x = gp_Vec(pulleys[0].frame.Location(), pulleys[1].frame.Location());
    x -= gp_Vec(n) * x.Dot(gp_Vec(n));
    if (x.SquareMagnitude() < 1e-12)
        throw std::runtime_error("The first two pulley axes coincide.");
    route.frame = gp_Ax3(pulleys[0].frame.Location().Translated(gp_Vec(n) * offset), n, gp_Dir(x));
    const gp_Vec ex(route.frame.XDirection()), ey(route.frame.YDirection());
    for (const auto& pulley : pulleys)
    {
        if (!std::isfinite(pulley.radius) || pulley.radius <= 1e-6 || pulley.radius > 1e9 ||
            (pulley.side != 1 && pulley.side != -1))
            throw std::runtime_error(
                "Pulley radii must be positive and routing sides must be +1 or -1.");
        if (!pulley.frame.Direction().IsParallel(n, 1e-6))
            throw std::runtime_error("Belt pulley axes must be parallel.");
        const gp_Vec delta(route.frame.Location(), pulley.frame.Location());
        route.centers.push_back({delta.Dot(ex), delta.Dot(ey)});
        route.radii.push_back(pulley.radius);
        route.sides.push_back(pulley.side);
    }
    const auto count = pulleys.size();
    double area = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto a = route.centers[i], b = route.centers[(i + 1) % count];
        area += a.x * b.y - a.y * b.x;
    }
    if (std::abs(area) > 1e-9)
        route.winding = area > 0 ? 1 : -1;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto j = (i + 1) % count;
        const Vec delta = route.centers[j] - route.centers[i];
        const double distance = delta.norm(),
                     difference = route.radii[i] * route.sides[i] - route.radii[j] * route.sides[j];
        if (distance <= std::abs(difference) + 1e-7)
            throw std::runtime_error("Pulley positions do not admit the requested belt tangency. "
                                     "Check order and routing sides.");
        const auto direction = delta * (1 / distance);
        const double cosine = difference / distance, sine = std::sqrt(1 - cosine * cosine);
        route.normals.push_back(direction * cosine +
                                Vec{-direction.y, direction.x} * (sine * -route.winding));
        route.length += std::sqrt(distance * distance - difference * difference);
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto previous = (i + count - 1) % count;
        const auto from = route.normals[previous] * route.sides[i],
                   to = route.normals[i] * route.sides[i];
        double angle = std::atan2(to.y, to.x) - std::atan2(from.y, from.x);
        if (route.sides[i] * route.winding < 0)
            angle = -angle;
        angle -= 2 * pi * std::floor(angle / (2 * pi));
        if (angle > 2 * pi - 1e-10)
            angle = 0;
        route.spans.push_back(angle);
        route.length += route.radii[i] * angle;
    }
    return route;
}
void append(Sketch& sketch, const Route& route, double offset, const char* prefix)
{
    const auto count = route.centers.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto previous = (i + count - 1) % count, j = (i + 1) % count;
        const double radius = route.radii[i] + route.sides[i] * offset;
        if (radius <= 1e-6)
            throw std::runtime_error("Belt thickness exceeds the available pulley radius.");
        const auto incoming = route.centers[i] +
                              route.normals[previous] * (route.sides[i] * route.radii[i] + offset),
                   outgoing = route.centers[i] +
                              route.normals[i] * (route.sides[i] * route.radii[i] + offset),
                   next = route.centers[j] +
                          route.normals[i] * (route.sides[j] * route.radii[j] + offset);
        if (route.spans[i] > 1e-9)
        {
            SketchEntity arc;
            arc.kind = EntityKind::Arc;
            arc.id = std::string(prefix) + "-arc-" + std::to_string(i);
            arc.center = route.centers[i].point();
            arc.first = incoming.point();
            arc.second = outgoing.point();
            arc.radius = radius;
            arc.clockwise = route.sides[i] * route.winding < 0;
            sketch.entities.push_back(arc);
        }
        SketchEntity line;
        line.kind = EntityKind::Line;
        line.id = std::string(prefix) + "-line-" + std::to_string(i);
        line.first = outgoing.point();
        line.second = next.point();
        sketch.entities.push_back(line);
    }
}
} // namespace
BeltGeometry beltGeometry(const std::vector<BeltPulley>& pulleys, double thickness,
                          double planeOffset)
{
    if (!std::isfinite(thickness) || thickness <= 0 || thickness > 1e9)
        throw std::runtime_error("Belt thickness must be positive.");
    const auto route = calculate(pulleys, planeOffset);
    BeltGeometry geometry;
    geometry.frame = route.frame;
    geometry.length = route.length;
    append(geometry.centerline, route, 0, "belt");
    append(geometry.boundary, route, thickness / 2, "left");
    append(geometry.boundary, route, -thickness / 2, "right");
    return geometry;
}
double beltLength(const std::vector<BeltPulley>& pulleys)
{
    return calculate(pulleys, 0).length;
}
} // namespace thecad::modeling::assembly
