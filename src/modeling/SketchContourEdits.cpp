#include "modeling/SketchContourEdits.h"
#include "modeling/DimensionAnnotation.h"
#include "modeling/SketchTools.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace thecad::modeling
{
namespace
{
constexpr double tolerance = 1e-6;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

Point2 subtract(Point2 a, Point2 b) { return {a.x - b.x, a.y - b.y}; }
double dot(Point2 a, Point2 b) { return a.x * b.x + a.y * b.y; }
double cross(Point2 a, Point2 b) { return a.x * b.y - a.y * b.x; }
double length(Point2 v) { return std::hypot(v.x, v.y); }
Point2 along(Point2 a, Point2 unit, double distance)
{
    return {a.x + unit.x * distance, a.y + unit.y * distance};
}

struct Intersection
{
    double distance;
    const SketchEntity* boundary;
};

std::vector<Intersection> intersections(const Sketch& sketch, const SketchEntity& line, Point2 unit)
{
    std::vector<Intersection> result;
    for (const auto& boundary : sketch.entities)
    {
        if (boundary.id == line.id)
            continue;
        const auto offset = subtract(boundary.first, line.first);
        if (boundary.kind == EntityKind::Line)
        {
            const auto direction = subtract(boundary.second, boundary.first);
            const double span = length(direction);
            if (span <= tolerance)
                continue;
            const double denominator = cross(unit, direction);
            if (std::abs(denominator) <= span * 1e-10)
                continue;
            const double fraction = cross(offset, unit) / denominator;
            if (fraction >= -tolerance / span && fraction <= 1 + tolerance / span)
                result.push_back({cross(offset, direction) / denominator, &boundary});
        }
        else if (boundary.kind == EntityKind::Circle)
        {
            const double distance = dot(offset, unit);
            const double height = std::abs(cross(unit, offset));
            if (height > boundary.radius + tolerance)
                continue;
            const double halfChord = std::sqrt(std::max(0., (boundary.radius - height) *
                                                              (boundary.radius + height)));
            result.push_back({distance - halfChord, &boundary});
            result.push_back({distance + halfChord, &boundary});
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.distance < b.distance;
    });
    result.erase(std::unique(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return std::abs(a.distance - b.distance) <= tolerance;
    }), result.end());
    return result;
}

std::string nextId(const Sketch& sketch, const char* prefix)
{
    for (int i = 1;; ++i)
    {
        const auto id = prefix + std::to_string(i);
        if (std::none_of(sketch.entities.begin(), sketch.entities.end(), [&](const auto& e) { return e.id == id; }) &&
            std::none_of(sketch.relations.begin(), sketch.relations.end(), [&](const auto& r) { return r.id == id; }) &&
            std::none_of(sketch.dimensions.begin(), sketch.dimensions.end(), [&](const auto& d) { return d.id == id; }))
            return id;
    }
}

void addRelation(Sketch& sketch, RelationKind kind, const SketchPointRef& first,
                 const SketchPointRef& second)
{
    sketch.relations.push_back({nextId(sketch, "line-edit-rel-"), kind, first, second});
}

void constrainIntersection(Sketch& sketch, const SketchPointRef& end, const SketchEntity& boundary)
{
    const bool circle = boundary.kind == EntityKind::Circle;
    addRelation(sketch, circle ? RelationKind::PointOnCircle : RelationKind::PointOnLine,
                end, {boundary.id, circle ? PointRole::Center : PointRole::First});
}

void transferEnd(const Sketch& original, Sketch& sketch, const std::string& first,
                 const std::string& second, bool split)
{
    const auto transfer = [&](SketchPointRef& point) {
        if (point.entityId == first && point.role == PointRole::Second)
            point.entityId = second;
    };
    for (auto& relation : sketch.relations)
    {
        if (relation.kind == RelationKind::Coincident)
        {
            transfer(relation.first);
            transfer(relation.second);
        }
        else if (relation.kind == RelationKind::FixedPoint || relation.kind == RelationKind::PointOnLine ||
                 relation.kind == RelationKind::PointOnCircle || relation.kind == RelationKind::Midpoint)
            transfer(relation.first);
    }
    for (auto& dimension : sketch.dimensions)
    {
        if (dimension.kind == DimensionKind::LineAngle || dimension.kind == DimensionKind::LineDistance)
            continue;
        if (dimension.first.entityId != first && dimension.second.entityId != first)
            continue;
        const auto annotation = dimensionAnnotationPosition(original, dimension);
        if (dimension.kind == DimensionKind::LineLength)
        {
            require(split || !dimension.driving, "A driving line length prevents removing the middle of this line.");
            dimension.kind = DimensionKind::PointDistance;
        }
        transfer(dimension.first);
        transfer(dimension.second);
        placeDimensionAnnotation(sketch, dimension, annotation);
    }
}

void verifyEditedLine(const Sketch& sketch)
{
    try
    {
        validateSketchRelations(sketch);
        (void)sketchShape(sketch);
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error(std::string("The edit conflicts with the sketch. ") + error.what());
    }
}
} // namespace

Sketch editSketchLine(const Sketch& sketch, SketchModifier kind, const std::string& id, Point2 pick)
{
    require(isSketchLineEdit(kind), "Choose Trim, Extend or Split.");
    require(std::isfinite(pick.x) && std::isfinite(pick.y), "Choose a finite point on the line.");
    const auto found = std::find_if(sketch.entities.begin(), sketch.entities.end(),
                                    [&](const auto& item) { return item.id == id; });
    require(found != sketch.entities.end() && found->kind == EntityKind::Line,
            "Pick a straight sketch line.");
    require(!sketchProjection(sketch, id) && !polygonForEntity(sketch, id) &&
                std::none_of(sketch.replications.begin(), sketch.replications.end(), [&](const auto& copy) {
                    return copy.source == id || copy.target == id || copy.mirrorLine == id;
                }) &&
                std::none_of(sketch.offsets.begin(), sketch.offsets.end(), [&](const auto& group) {
                    return std::any_of(group.segments.begin(), group.segments.end(), [&](const auto& segment) {
                        return segment.source == id || segment.target == id;
                    });
                }),
            "This line belongs to linked or generated sketch geometry.");
    for (const auto& relation : sketch.relations)
        if (relation.first.entityId == id || relation.second.entityId == id)
            require(relation.kind != RelationKind::FixedEntity && relation.kind != RelationKind::Equal &&
                        relation.kind != RelationKind::EqualCurveLength &&
                        !(relation.kind == RelationKind::Midpoint && relation.second.entityId == id),
                    "A Fix, Equal or Midpoint relation controls this line. Release it before editing its length.");

    const auto vector = subtract(found->second, found->first);
    const double span = length(vector);
    require(span > tolerance, "The line is too short to edit.");
    const Point2 unit{vector.x / span, vector.y / span};
    const double position = std::clamp(dot(subtract(pick, found->first), unit), 0., span);
    auto result = sketch;
    const auto index = std::size_t(found - sketch.entities.begin());
    const auto pointAt = [&](double distance) { return along(found->first, unit, distance); };
    const auto split = [&](double left, double right, bool keepJoint) {
        require(result.entities.size() < 1000, "The sketch has reached its entity limit.");
        auto second = *found;
        second.id = nextId(result, "line-piece-");
        second.first = pointAt(right);
        result.entities[index].second = pointAt(left);
        result.entities.push_back(second);
        transferEnd(sketch, result, id, second.id, keepJoint);
        if (keepJoint)
        {
            addRelation(result, RelationKind::Coincident, {id, PointRole::Second},
                        {second.id, PointRole::First});
            addRelation(result, RelationKind::Parallel, {id, PointRole::First},
                        {second.id, PointRole::First});
        }
        else
            addRelation(result, RelationKind::Collinear, {id, PointRole::First},
                        {second.id, PointRole::First});
        return second.id;
    };
    if (kind == SketchModifier::Split)
    {
        require(position > tolerance && position < span - tolerance,
                "Pick a point inside the line to split it.");
        split(position, position, true);
    }
    else
    {
        const auto hits = intersections(sketch, *found, unit);
        if (kind == SketchModifier::Extend)
        {
            const bool atFirst = position < span / 2;
            const Intersection* nearest = nullptr;
            for (const auto& hit : hits)
            {
                if (atFirst && hit.distance < -tolerance)
                    nearest = &hit;
                if (!atFirst && hit.distance > span + tolerance)
                {
                    nearest = &hit;
                    break;
                }
            }
            require(nearest, "There is no intersection beyond that end of the line.");
            auto& line = result.entities[index];
            (atFirst ? line.first : line.second) = pointAt(nearest->distance);
            constrainIntersection(result, {id, atFirst ? PointRole::First : PointRole::Second},
                                  *nearest->boundary);
        }
        else
        {
            const Intersection *left = nullptr, *right = nullptr;
            for (const auto& hit : hits)
            {
                if (hit.distance <= tolerance || hit.distance >= span - tolerance)
                    continue;
                require(std::abs(hit.distance - position) > tolerance,
                        "Pick the section between intersections to trim.");
                if (hit.distance < position)
                    left = &hit;
                else
                {
                    right = &hit;
                    break;
                }
            }
            require(left || right, "No interior intersection was found on the selected line.");
            if (left && right)
            {
                const auto second = split(left->distance, right->distance, false);
                constrainIntersection(result, {id, PointRole::Second}, *left->boundary);
                constrainIntersection(result, {second, PointRole::First}, *right->boundary);
            }
            else if (left)
            {
                result.entities[index].second = pointAt(left->distance);
                constrainIntersection(result, {id, PointRole::Second}, *left->boundary);
            }
            else
            {
                result.entities[index].first = pointAt(right->distance);
                constrainIntersection(result, {id, PointRole::First}, *right->boundary);
            }
        }
    }
    verifyEditedLine(result);
    return result;
}
} // namespace thecad::modeling
