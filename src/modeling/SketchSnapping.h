#pragma once
#include "modeling/Sketch.h"
#include <cmath>
#include <algorithm>
#include <optional>
#include <vector>

namespace thecad::modeling {
// Screen coordinates and tolerance must use the same pixel units. Projection is
// supplied by the caller so this picking policy has no UI or camera dependency.
struct ProjectedSketchPoint { SketchPointRef reference; Point2 model; double x; double y; };
inline std::optional<ProjectedSketchPoint> nearestSketchPoint(
    const std::vector<ProjectedSketchPoint>& candidates, double x, double y, double tolerance)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(tolerance) || tolerance < 0) return {};
    double best = tolerance * tolerance;
    std::optional<ProjectedSketchPoint> result;
    for (const auto& candidate : candidates) {
        const double dx = candidate.x - x, dy = candidate.y - y;
        const double distance = dx * dx + dy * dy;
        if (std::isfinite(distance) && distance <= best && (!result || distance < best)) {
            best = distance; result = candidate;
        }
    }
    return result;
}
struct LineInference { Point2 point; std::optional<RelationKind> relation; };
inline LineInference inferLineDirection(Point2 first, Point2 raw, double horizontalPixels,
    double verticalPixels, double lengthPixels, const std::optional<RelationKind>& previous = {})
{
    LineInference result{raw,{}};
    if(!std::isfinite(lengthPixels) || lengthPixels<14) return result;
    const double acquire=std::min(7.0,lengthPixels*0.08),release=std::min(11.0,lengthPixels*0.12);
    if(previous==RelationKind::Horizontal && horizontalPixels<=release) result.relation=RelationKind::Horizontal;
    else if(previous==RelationKind::Vertical && verticalPixels<=release) result.relation=RelationKind::Vertical;
    else if(horizontalPixels<=acquire && horizontalPixels<=verticalPixels) result.relation=RelationKind::Horizontal;
    else if(verticalPixels<=acquire) result.relation=RelationKind::Vertical;
    if(result.relation==RelationKind::Horizontal) result.point.y=first.y;
    if(result.relation==RelationKind::Vertical) result.point.x=first.x;
    return result;
}
} // namespace thecad::modeling
