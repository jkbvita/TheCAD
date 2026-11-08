#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <optional>
#include <utility>

namespace thecad::modeling
{
struct Measurement
{
    std::optional<double> length, radius, distance, angleDegrees, centerDistance;
    std::optional<gp_Pnt> coordinates, anchor;
    std::optional<std::pair<gp_Pnt, gp_Pnt>> span;
};

// One edge reports its length and, for circular geometry, its radius.
// A pair reports closest distance and applicable angle or center spacing.
// Returns no result for unsupported shapes or a failed geometric query.
std::optional<Measurement> measureGeometry(const TopoDS_Shape& first,
                                           const TopoDS_Shape& second = {});
}
