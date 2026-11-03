#pragma once
#include "modeling/Sketch.h"
#include <cmath>
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
} // namespace thecad::modeling
