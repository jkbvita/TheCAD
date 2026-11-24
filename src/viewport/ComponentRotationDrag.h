#pragma once
#include <QPointF>
#include <gp_Dir.hxx>
#include <gp_Quaternion.hxx>
#include <algorithm>
#include <cmath>

namespace thecad::viewport
{
// Logical screen coordinates keep the gesture independent of display scaling.
inline gp_Quaternion componentRotationDrag(QPointF from, QPointF to, QPointF center,
                                           double radius, gp_Dir viewDirection, gp_Dir up)
{
    const gp_Vec eye(-gp_Vec(viewDirection));
    const auto right = gp_Vec(viewDirection).Crossed(gp_Vec(up)).Normalized();
    const auto vertical = eye.Crossed(right).Normalized();
    const auto sphere = [&](QPointF point)
    {
        double x = (point.x() - center.x()) / std::max(1.0, radius);
        double y = (center.y() - point.y()) / std::max(1.0, radius);
        const double squared = x * x + y * y;
        double z = 0;
        if (squared <= 1) z = std::sqrt(1 - squared);
        else { const double length = std::sqrt(squared); x /= length; y /= length; }
        return right * x + vertical * y + eye * z;
    };
    return gp_Quaternion(sphere(from), sphere(to));
}
} // namespace thecad::viewport
