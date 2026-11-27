#pragma once

#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <cmath>
#include <optional>

namespace thecad::viewport
{
// Intersect a forward or backward infinite ray with a plane. Returns no point
// for a parallel ray; callers can then preserve their current focus point.
inline std::optional<gp_Pnt> intersectRayWithPlane(const gp_Pnt& rayOrigin,
                                                   const gp_Dir& rayDirection,
                                                   const gp_Pnt& planePoint,
                                                   const gp_Dir& planeNormal)
{
    const gp_Vec direction(rayDirection);
    const gp_Vec normal(planeNormal);
    const double denominator = direction.Dot(normal);
    if (std::abs(denominator) < 1e-12)
        return {};

    const double distance = gp_Vec(rayOrigin, planePoint).Dot(normal) / denominator;
    return rayOrigin.Translated(direction * distance);
}
} // namespace thecad::viewport
