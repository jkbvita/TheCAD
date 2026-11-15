#include "modeling/assembly/MateEquations.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace thecad::modeling::assembly
{
namespace
{
using K = MateGeometryKind;
struct Set
{
    gp_Pnt center;
    gp_Vec normal, axis;
    double width = 0, opening = 0;
    bool cylindrical = false, wedge = false;
};
Set selectionSet(const std::vector<const ResolvedMateReference*>& refs,
                 const std::vector<gp_Trsf>& poses)
{
    if (refs.empty() || refs.size() > 2)
        throw std::runtime_error(
            "Width requires two planar faces or one axis/cylindrical face in each selection set.");
    const auto transform = [&](const auto* ref)
    {
        auto frame = ref->geometry.frame;
        if (ref->body >= 0)
            frame.Transform(poses.at(std::size_t(ref->body)));
        return frame;
    };
    const auto a = transform(refs[0]);
    Set result;
    result.center = a.Location();
    if (refs.size() == 1)
    {
        const auto& g = refs[0]->geometry;
        if (g.kind != K::Line && g.kind != K::Cylinder)
            throw std::runtime_error(
                "A single Width reference must be an axis or cylindrical face.");
        result.cylindrical = true;
        result.axis = gp_Vec(a.Direction());
        result.width = 2 * g.radius;
        return result;
    }
    if (refs[0]->body != refs[1]->body || refs[0]->geometry.kind != K::Plane ||
        refs[1]->geometry.kind != K::Plane)
        throw std::runtime_error("Each Width face pair must be on one component and planar.");
    const auto b = transform(refs[1]);
    const double sign =
        refs[0]->geometry.frame.Direction().Dot(refs[1]->geometry.frame.Direction()) < 0 ? -1 : 1;
    const gp_Vec n(a.Direction()), m = gp_Vec(b.Direction()) * sign;
    const auto sum = n + m;
    if (sum.SquareMagnitude() < 1e-12)
        throw std::runtime_error("Ambiguous width bisector.");
    result.normal = sum.Normalized();
    result.wedge = n.Crossed(m).SquareMagnitude() > 1e-12;
    result.opening = std::atan2(n.Crossed(m).Magnitude(), n.Dot(m));
    const double offset =
        (gp_Vec(a.Location().XYZ()).Dot(n) + gp_Vec(b.Location().XYZ()).Dot(m)) / sum.Magnitude();
    result.center = gp_Pnt(result.normal.XYZ() * offset);
    result.width = std::abs(gp_Vec(a.Location(), b.Location()).Dot(result.normal));
    if (result.wedge)
    {
        result.axis = n.Crossed(m).Normalized();
        const double d1 = gp_Vec(a.Location().XYZ()).Dot(n), d2 = gp_Vec(b.Location().XYZ()).Dot(m),
                     dot = n.Dot(m), denominator = 1 - dot * dot;
        result.center = gp_Pnt(
            (n * ((d1 - dot * d2) / denominator) + m * ((d2 - dot * d1) / denominator)).XYZ());
    }
    return result;
}
} // namespace
MateResidual widthResidual(const ResolvedMate& mate, const std::vector<gp_Trsf>& poses,
                           double scale, bool diagnose)
{
    const auto& d = *mate.definition;
    std::vector<const ResolvedMateReference*> widths, tabs;
    for (const auto& ref : d.references)
        if (ref.role == "width")
            widths.push_back(&ref);
        else if (ref.role == "tab")
            tabs.push_back(&ref);
    auto width = selectionSet(widths, poses), tab = selectionSet(tabs, poses);
    const auto& p = d.parameters.position;
    MateResidual result;
    const auto scalar = [&](double v, bool length = false)
    {
        result.values.push_back(length ? v / scale : v);
        result.tolerances.push_back(length ? 1e-5 / scale : 1e-8);
    };
    const auto vector = [&](const gp_Vec& v, bool length = false)
    {
        scalar(v.X(), length);
        scalar(v.Y(), length);
        scalar(v.Z(), length);
    };
    const auto bound = [&](double v, double extent, bool length)
    { scalar(diagnose ? 0 : std::max(0., std::abs(v) - extent / 2) * (v < 0 ? -1 : 1), length); };
    if (width.cylindrical && tab.cylindrical)
    {
        if (p.mode != PositionMode::Centered)
            throw std::runtime_error("Two axis/cylinder Width sets support Centered only.");
        vector(tab.axis - width.axis * mate.alignment);
        vector(gp_Vec(width.center, tab.center).Crossed(width.axis), true);
        return result;
    }
    if (width.cylindrical)
    {
        std::swap(width, tab);
    }
    if (tab.cylindrical)
    {
        scalar(tab.axis.Dot(width.normal));
        const double available = width.width - tab.width,
                     offset =
                         gp_Vec(width.center, tab.center).Dot(width.normal) * (p.flip ? -1 : 1);
        if (width.wedge)
        {
            if (p.mode != PositionMode::Centered)
                throw std::runtime_error(
                    "Axis/cylinder in a nonparallel Width set currently supports Centered only.");
            scalar(offset, true);
            return result;
        }
        if (available < -1e-6)
            throw std::runtime_error("The cylindrical tab is wider than the groove.");
        if (p.mode == PositionMode::Centered)
            scalar(offset, true);
        else if (p.mode == PositionMode::Free)
            bound(offset, available, true);
        else
            scalar(
                offset + available / 2 -
                    (p.mode == PositionMode::Distance ? p.distance : available * p.percent / 100),
                true);
        return result;
    }
    if (width.wedge || tab.wedge)
    {
        if (p.mode == PositionMode::Centered)
        {
            vector(tab.normal - width.normal * mate.alignment);
            scalar(gp_Vec(width.center, tab.center).Dot(width.normal), true);
            return result;
        }
        const double available = width.opening - tab.opening;
        if (available < 1e-8)
            throw std::runtime_error("Equal or narrower angular widths support Centered only.");
        scalar(tab.normal.Dot(width.axis));
        const double angle = std::atan2(width.normal.Crossed(tab.normal).Dot(width.axis),
                                        width.normal.Dot(tab.normal) * mate.alignment) *
                             (p.flip ? -1 : 1);
        // A tab remains on the groove's intersection line while its angle varies.
        vector(gp_Vec(width.center, tab.center).Crossed(width.axis), true);
        if (p.mode == PositionMode::Free)
            bound(angle, available, false);
        else
            scalar(angle + available / 2 -
                   (p.mode == PositionMode::Distance ? p.distance * 3.141592653589793 / 180
                                                     : available * p.percent / 100));
        return result;
    }
    vector(tab.normal - width.normal * mate.alignment);
    const double available = width.width - tab.width;
    if (available < -1e-6)
        throw std::runtime_error("The tab is wider than the available groove.");
    const double offset = gp_Vec(width.center, tab.center).Dot(width.normal) * (p.flip ? -1 : 1);
    if (p.mode == PositionMode::Free)
        bound(offset, available, true);
    else if (p.mode == PositionMode::Centered)
        scalar(offset, true);
    else
        scalar(offset + available / 2 -
                   (p.mode == PositionMode::Distance ? p.distance : available * p.percent / 100),
               true);
    return result;
}
} // namespace thecad::modeling::assembly
