#include "modeling/assembly/MateEquations.h"
#include "modeling/assembly/BeltGeometry.h"
#include <BRepClass_FaceClassifier.hxx>
#include <TopoDS.hxx>
#include <gp_Pnt2d.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Plane.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <gp_Quaternion.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace thecad::modeling::assembly
{
namespace
{
constexpr double pi = 3.14159265358979323846;
using K = MateGeometryKind;
gp_Trsf pose(int body, const std::vector<gp_Trsf>& poses)
{
    return body < 0 ? gp_Trsf() : poses.at(std::size_t(body));
}
struct World
{
    const MateGeometry& g;
    gp_Trsf placement;
    gp_Ax3 frame;
    gp_Pnt c;
    gp_Vec n, x;
    World(const ResolvedMateReference& r, const std::vector<gp_Trsf>& p)
        : g(r.geometry), placement(pose(r.body, p)), frame(g.frame), c(frame.Location()),
          n(frame.Direction()), x(frame.XDirection())
    {
        frame.Transform(placement);
        c = frame.Location();
        n = gp_Vec(frame.Direction());
        x = gp_Vec(frame.XDirection());
    }
};
bool point(const MateGeometry& g)
{
    return g.kind == K::Point || g.kind == K::Frame || g.kind == K::Sphere;
}
bool axis(const MateGeometry& g)
{
    return g.kind == K::Line || g.kind == K::Cylinder || g.kind == K::Cone || g.kind == K::Circle;
}
gp_Vec gap(const World& a, const World& b)
{
    return gp_Vec(a.c, b.c);
}
double axesDistance(const World& a, const World& b)
{
    const auto cross = a.n.Crossed(b.n);
    return cross.SquareMagnitude() < 1e-12 ? gap(a, b).Crossed(a.n).Magnitude()
                                           : std::abs(gap(a, b).Dot(cross.Normalized()));
}
double distance(const World& a, const World& b)
{
    if (a.g.kind == K::Plane && b.g.kind == K::Plane)
        return gap(a, b).Dot(a.n);
    if (a.g.kind == K::Plane)
        return std::abs(gap(a, b).Dot(a.n));
    if (b.g.kind == K::Plane)
        return std::abs(gap(a, b).Dot(b.n));
    if (axis(a.g) && axis(b.g))
        return axesDistance(a, b);
    if (axis(a.g))
        return gap(a, b).Crossed(a.n).Magnitude();
    if (axis(b.g))
        return gap(a, b).Crossed(b.n).Magnitude();
    return a.c.Distance(b.c);
}
double angle(const World& a, const World& b)
{
    return std::atan2(a.n.Crossed(b.n).Magnitude(), a.n.Dot(b.n));
}
gp_Vec reflect(const gp_Vec& v, const gp_Vec& n)
{
    return v - n * (2 * v.Dot(n));
}
double phase(const gp_Trsf& current, const gp_Trsf& anchor, const gp_Dir& direction)
{
    const auto q = current.GetRotation() * anchor.GetRotation().Inverted();
    return 2 * std::atan2(gp_Vec(q.X(), q.Y(), q.Z()).Dot(gp_Vec(direction)), q.W());
}
double relativeScrewPhase(const ResolvedMateDefinition& definition,
                          const std::vector<gp_Trsf>& poses)
{
    const auto& a = definition.references[0];
    const auto& b = definition.references[1];
    const auto relative = pose(a.body, poses).Inverted() * pose(b.body, poses),
               anchor = definition.motion.anchors[0].Inverted() * definition.motion.anchors[1];
    return phase(relative, anchor, a.geometry.frame.Direction());
}
gp_Pnt surfacePoint(const World& a, double u, double v, gp_Vec* normal = nullptr)
{
    const double pu = a.g.u0 + u * (a.g.u1 - a.g.u0), pv = a.g.v0 + v * (a.g.v1 - a.g.v0);
    gp_Pnt p;
    gp_Vec du, dv;
    a.g.surface->D1(pu, pv, p, du, dv);
    if (normal)
    {
        auto n = du.Crossed(dv);
        if (n.SquareMagnitude() < 1e-20)
            throw std::runtime_error("Mate contact lies at a singular surface normal.");
        n.Normalize();
        if (a.g.reversed)
            n.Reverse();
        n.Transform(a.placement);
        *normal = n;
    }
    return p.Transformed(a.placement);
}
std::vector<const ResolvedMateReference*> role(const ResolvedMateDefinition& d, const char* name)
{
    std::vector<const ResolvedMateReference*> r;
    for (const auto& e : d.references)
        if (e.role == name)
            r.push_back(&e);
    return r;
}
struct Eval
{
    MateResidual result;
    double scale;
    bool diagnostic;
    void scalar(double v, bool length = false)
    {
        result.values.push_back(length ? v / scale : v);
        result.tolerances.push_back(length ? 1e-5 / scale : 1e-8);
    }
    void vector(const gp_Vec& v, bool length = false)
    {
        scalar(v.X(), length);
        scalar(v.Y(), length);
        scalar(v.Z(), length);
    }
    void align(const World& a, const World& b, int sign = 1) { vector(b.n - a.n * sign); }
    void bound(double x, double low, double high, bool length = false)
    {
        scalar(low == high  ? x - low
               : diagnostic ? 0
               : x < low    ? x - low
               : x > high   ? x - high
                            : 0,
               length);
    }
    void coincident(const World& a, const World& b, int sign)
    {
        if (a.g.kind == K::Plane && b.g.kind == K::Plane)
        {
            align(a, b, sign);
            scalar(gap(a, b).Dot(a.n), true);
        }
        else if ((a.g.kind == K::Circle && b.g.kind == K::Plane) ||
                 (b.g.kind == K::Circle && a.g.kind == K::Plane))
        {
            align(a, b, sign);
            scalar(gap(a, b).Dot(a.g.kind == K::Plane ? a.n : b.n), true);
        }
        else if ((a.g.kind == K::Line && b.g.kind == K::Cylinder) ||
                 (b.g.kind == K::Line && a.g.kind == K::Cylinder))
        {
            align(a, b, sign);
            scalar(gap(a, b).Crossed(a.n).Magnitude() -
                       (a.g.kind == K::Cylinder ? a.g.radius : b.g.radius),
                   true);
        }
        else if ((a.g.kind == K::Circle && (b.g.kind == K::Cylinder || b.g.kind == K::Cone)) ||
                 (b.g.kind == K::Circle && (a.g.kind == K::Cylinder || a.g.kind == K::Cone)))
        {
            const auto& circle = a.g.kind == K::Circle ? a : b;
            const auto& surface = a.g.kind == K::Circle ? b : a;
            align(a, b, sign);
            vector(gap(a, b).Crossed(a.n), true);
            scalar(circle.g.radius -
                       std::abs(surface.g.radius + gp_Vec(surface.c, circle.c).Dot(surface.n) *
                                                       std::tan(surface.g.semiAngle)),
                   true);
        }
        else if (a.g.kind == K::Plane && axis(b.g))
        {
            scalar(a.n.Dot(b.n));
            scalar(gap(a, b).Dot(a.n), true);
        }
        else if (b.g.kind == K::Plane && axis(a.g))
        {
            scalar(b.n.Dot(a.n));
            scalar(gap(a, b).Dot(b.n), true);
        }
        else if (a.g.kind == K::Cone && b.g.kind == K::Cone)
        {
            if (std::abs(a.g.semiAngle - b.g.semiAngle * sign) > 1e-6)
                throw std::runtime_error("Coincident cones must have compatible half angles.");
            align(a, b, sign);
            vector(gap(a, b).Crossed(a.n), true);
            scalar(a.g.radius + gap(a, b).Dot(a.n) * std::tan(a.g.semiAngle) - b.g.radius, true);
        }
        else if (axis(a.g) && axis(b.g))
        {
            align(a, b, sign);
            vector(gap(a, b).Crossed(a.n), true);
        }
        else if (a.g.kind == K::Plane)
            scalar(gap(a, b).Dot(a.n), true);
        else if (b.g.kind == K::Plane)
            scalar(gap(a, b).Dot(b.n), true);
        else if (a.g.kind == K::Sphere)
            scalar(a.c.Distance(b.c) - a.g.radius, true);
        else if (b.g.kind == K::Sphere)
            scalar(a.c.Distance(b.c) - b.g.radius, true);
        else if (axis(a.g))
        {
            const auto v = gap(a, b);
            if (a.g.kind == K::Cylinder)
                scalar(v.Crossed(a.n).Magnitude() - a.g.radius, true);
            else if (a.g.kind == K::Cone)
                scalar(v.Crossed(a.n).Magnitude() -
                           std::abs(a.g.radius + v.Dot(a.n) * std::tan(a.g.semiAngle)),
                       true);
            else
                vector(v.Crossed(a.n), true);
        }
        else if (axis(b.g))
        {
            const auto v = -gap(a, b);
            if (b.g.kind == K::Cylinder)
                scalar(v.Crossed(b.n).Magnitude() - b.g.radius, true);
            else if (b.g.kind == K::Cone)
                scalar(v.Crossed(b.n).Magnitude() -
                           std::abs(b.g.radius + v.Dot(b.n) * std::tan(b.g.semiAngle)),
                       true);
            else
                vector(v.Crossed(b.n), true);
        }
        else
            vector(gap(a, b), true);
    }
};
bool surfaceContact(const MateGeometry& a, const MateGeometry& b)
{
    return a.kind == K::Surface || b.kind == K::Surface || a.kind == K::Cone || b.kind == K::Cone;
}
} // namespace
bool compatibleMate(MateKind kind, const MateGeometry& a, const MateGeometry& b)
{
    const auto first = a.kind == K::Frame ? K::Point : a.kind,
               second = b.kind == K::Frame ? K::Point : b.kind;
    const auto pair = [&](K x, K y)
    { return (first == x && second == y) || (first == y && second == x); };
    const bool directions = (a.kind == K::Line || a.kind == K::Cylinder || a.kind == K::Cone) &&
                            (b.kind == K::Line || b.kind == K::Cylinder || b.kind == K::Cone);
    if (kind == MateKind::Lock || kind == MateKind::InPlace || kind == MateKind::LockToSketch)
        return true;
    if (kind == MateKind::Magnetic)
        return a.kind == K::Frame && b.kind == K::Frame;
    if (kind == MateKind::ProfileCenter)
        return a.profile != MateProfileKind::None && a.profile != MateProfileKind::Slot &&
               b.profile != MateProfileKind::None && b.profile != MateProfileKind::Slot;
    if (kind == MateKind::Slot)
        return a.profile == MateProfileKind::Slot &&
               (b.kind == K::Line || b.kind == K::Cylinder || b.profile == MateProfileKind::Slot);
    if (kind == MateKind::Concentric)
        return directions || pair(K::Circle, K::Circle) || pair(K::Circle, K::Line) ||
               pair(K::Circle, K::Cylinder) || pair(K::Circle, K::Cone) ||
               pair(K::Sphere, K::Sphere) || pair(K::Point, K::Sphere) ||
               pair(K::Point, K::Cylinder) || pair(K::Point, K::Cone) || pair(K::Line, K::Sphere) ||
               pair(K::Cylinder, K::Sphere);
    if (kind == MateKind::Parallel || kind == MateKind::Perpendicular || kind == MateKind::Angle ||
        kind == MateKind::LimitAngle)
    {
        if (directions || pair(K::Plane, K::Plane))
            return true;
        return (kind == MateKind::Parallel || kind == MateKind::Perpendicular) &&
               pair(K::Plane, K::Line);
    }
    if (kind == MateKind::Distance || kind == MateKind::LimitDistance)
        return pair(K::Point, K::Point) || pair(K::Point, K::Plane) || pair(K::Point, K::Line) ||
               pair(K::Point, K::Cylinder) || pair(K::Point, K::Sphere) || pair(K::Line, K::Line) ||
               pair(K::Line, K::Plane) || pair(K::Line, K::Cylinder) || pair(K::Line, K::Sphere) ||
               pair(K::Plane, K::Plane) || pair(K::Plane, K::Cylinder) ||
               pair(K::Plane, K::Sphere) || pair(K::Cylinder, K::Cylinder) ||
               pair(K::Sphere, K::Sphere) || pair(K::Cone, K::Cone);
    if (kind == MateKind::Tangent)
        return pair(K::Plane, K::Cylinder) || pair(K::Plane, K::Sphere) ||
               pair(K::Plane, K::Cone) || pair(K::Plane, K::Surface) ||
               pair(K::Line, K::Cylinder) || pair(K::Line, K::Sphere) ||
               pair(K::Cylinder, K::Cylinder) || pair(K::Cylinder, K::Sphere) ||
               pair(K::Sphere, K::Sphere) || pair(K::Cone, K::Sphere) ||
               pair(K::Surface, K::Cylinder) || pair(K::Surface, K::Cone);
    if (kind == MateKind::Coincident)
        return pair(K::Point, K::Point) || pair(K::Point, K::Plane) || pair(K::Point, K::Line) ||
               pair(K::Point, K::Cylinder) || pair(K::Point, K::Cone) ||
               pair(K::Point, K::Sphere) || pair(K::Point, K::Curve) ||
               pair(K::Point, K::Surface) || pair(K::Plane, K::Plane) || pair(K::Line, K::Plane) ||
               pair(K::Line, K::Line) || pair(K::Line, K::Cylinder) || pair(K::Cone, K::Cone) ||
               pair(K::Circle, K::Plane) || pair(K::Circle, K::Cylinder) ||
               pair(K::Circle, K::Cone) || pair(K::Frame, K::Frame);
    if (kind == MateKind::RackPinion)
        return a.kind == K::Line && axis(b);
    if (kind == MateKind::Gear || kind == MateKind::Screw || kind == MateKind::UniversalJoint ||
        kind == MateKind::LinearCoupler)
        return axis(a) && axis(b);
    return true;
}
void prepareMate(ResolvedMate& mate, const std::vector<gp_Trsf>& poses)
{
    if (!mate.definition)
        return;
    auto& d = *mate.definition;
    validateMateParameters(mate.kind, d.parameters);
    const auto roles = mateRoles(mate.kind, d.parameters.hinge.limits);
    for (const auto& r : roles)
    {
        const auto n = role(d, r.key.c_str()).size();
        if (n < std::size_t(r.minimum) || n > std::size_t(r.maximum))
            throw std::runtime_error(
                r.label + ": select " + std::to_string(r.minimum) +
                (r.minimum == r.maximum ? "" : " to " + std::to_string(r.maximum)) +
                " reference(s).");
    }
    for (const auto& r : d.references)
    {
        if (r.body < -1 || r.body >= int(poses.size()))
            throw std::runtime_error("Mate reference component is unavailable.");
        if (std::none_of(roles.begin(), roles.end(),
                         [&](const auto& expected) { return expected.key == r.role; }))
            throw std::runtime_error("Unexpected mate reference role: " + r.role);
    }
    if (d.references.size() == 2 &&
        !compatibleMate(mate.kind, d.references[0].geometry, d.references[1].geometry))
        throw std::runtime_error("The selected entity types do not support this mate.");
    if (!d.references.empty() &&
        std::all_of(d.references.begin(), d.references.end(),
                    [&](const auto& ref) { return ref.body == d.references[0].body; }))
        throw std::runtime_error(
            "Mate references must involve different components or the assembly origin.");
    if (mate.kind == MateKind::Angle && (mate.value < 0 || mate.value > 180))
        throw std::runtime_error("Angle mates must lie between 0 and 180 degrees.");
    if (mate.kind == MateKind::Coincident && d.parameters.standard.alignAxes &&
        (d.references[0].geometry.kind != K::Frame || d.references[1].geometry.kind != K::Frame))
        throw std::runtime_error(
            "Align coordinate axes requires two origins or coordinate frames.");
    if (mate.kind == MateKind::Path)
    {
        const auto point = role(d, "point")[0];
        if (point->geometry.kind != K::Point && point->geometry.kind != K::Frame)
            throw std::runtime_error(
                "Path Mate requires a component vertex, sketch point or origin.");
        const auto up = role(d, "up");
        if (!up.empty() && !mateGeometryDirection(up[0]->geometry))
            throw std::runtime_error("Choose a plane or axis for the Path Mate up vector.");
    }
    if (mate.kind == MateKind::Cam)
    {
        const auto kind = role(d, "follower")[0]->geometry.kind;
        if (kind != K::Point && kind != K::Frame && kind != K::Cylinder && kind != K::Plane)
            throw std::runtime_error(
                "A cam follower must be a vertex, point, planar face or cylindrical face.");
    }
    if (mate.kind == MateKind::Slot &&
        role(d, "follower")[0]->geometry.profile == MateProfileKind::Slot &&
        int(d.parameters.position.mode) > int(PositionMode::Centered))
        throw std::runtime_error("Slot-to-slot mating supports Free or Centered positioning.");
    if (mate.kind == MateKind::Hinge)
    {
        const auto axes = role(d, "concentric"), planes = role(d, "coincident");
        if (!compatibleMate(MateKind::Concentric, axes[0]->geometry, axes[1]->geometry))
            throw std::runtime_error(
                "Select two axes, circular edges or cylindrical faces for the hinge.");
        for (const auto* plane : planes)
        {
            const auto own = std::find_if(axes.begin(), axes.end(), [&](const auto* axis)
                                          { return axis->body == plane->body; });
            if (own == axes.end() || plane->geometry.kind != K::Plane ||
                !plane->geometry.frame.Direction().IsParallel((*own)->geometry.frame.Direction(),
                                                              1e-6))
                throw std::runtime_error("Hinge seating planes must belong to the hinged "
                                         "components and be normal to their concentric axes.");
        }
    }
    if (mate.kind == MateKind::Belt)
    {
        const auto n = d.references.size();
        if ((!d.parameters.belt.diameters.empty() && d.parameters.belt.diameters.size() != n) ||
            (!d.parameters.belt.sides.empty() && d.parameters.belt.sides.size() != n))
            throw std::runtime_error("Belt pulley parameter counts do not match their references.");
        for (const auto& ref : d.references)
            if (!axis(ref.geometry))
                throw std::runtime_error(
                    "Belt members must be cylindrical faces, circular edges or axes.");
    }
    if (mate.kind == MateKind::UniversalJoint)
    {
        const World a(d.references[0], poses), b(d.references[1], poses);
        if (std::abs(a.n.Dot(b.n)) < 1e-6)
            throw std::runtime_error("A universal joint is singular at a 90 degree shaft angle.");
    }
    const bool initialized = d.motion.initialized;
    if (initialized)
    {
        if (d.motion.anchors.size() != d.references.size())
            throw std::runtime_error("Invalid mate motion anchors.");
        for (const auto& anchor : d.motion.anchors)
            validateRigidPlacement(anchor);
    }
    else
    {
        d.motion.anchors.clear();
        for (const auto& ref : d.references)
            d.motion.anchors.push_back(pose(ref.body, poses));
        d.motion.coordinates.clear();
    }
    if (mate.kind == MateKind::Path || mate.kind == MateKind::Cam || mate.kind == MateKind::Slot)
    {
        const auto curveRole = mate.kind == MateKind::Path  ? "path"
                               : mate.kind == MateKind::Cam ? "cam"
                                                            : "slot";
        const auto curves = role(d, curveRole);
        std::shared_ptr<const MatePath> path;
        if (curves.size() == 1 && curves[0]->geometry.path)
            path = curves[0]->geometry.path;
        else
        {
            std::vector<MateGeometry> chain;
            for (const auto* ref : curves)
            {
                if (ref->body != curves[0]->body)
                    throw std::runtime_error(
                        "All path or cam references must belong to the same component.");
                chain.push_back(ref->geometry);
            }
            if (mate.kind == MateKind::Cam)
            {
                gp_Dir direction;
                path = camPath(chain, direction);
                for (auto& ref : d.references)
                    if (ref.role == curveRole)
                        ref.geometry.frame.SetDirection(direction);
            }
            else
                path = matePath(chain);
        }
        for (auto& ref : d.references)
            if (ref.role == curveRole)
            {
                ref.geometry.path = path;
                break;
            }
        if (initialized)
        {
            if (d.motion.coordinates.size() !=
                (mate.kind == MateKind::Slot &&
                         role(d, "follower")[0]->geometry.profile == MateProfileKind::Slot
                     ? 2u
                     : 1u))
                throw std::runtime_error("Invalid path motion state.");
            return;
        }
        const auto followers = role(d, mate.kind == MateKind::Path ? "point" : "follower");
        const World follower(*followers[0], poses);
        const auto transform = pose(curves[0]->body, poses);
        double best = 1e100, s = 0;
        for (int i = 0; i <= 64; ++i)
        {
            const auto p = path->point(i / 64.0).Transformed(transform);
            const double distance = p.SquareDistance(follower.c);
            if (distance < best)
            {
                best = distance;
                s = i / 64.0;
            }
        }
        d.motion.coordinates.push_back(s);
        if (mate.kind == MateKind::Slot && followers[0]->geometry.profile == MateProfileKind::Slot)
            d.motion.coordinates.push_back(0.5);
    }
    else if (mate.kind == MateKind::Tangent &&
             surfaceContact(d.references[0].geometry, d.references[1].geometry))
    {
        for (auto& ref : d.references)
        {
            auto& g = ref.geometry;
            if (g.surface.IsNull())
            {
                if (g.kind == K::Plane)
                    g.surface = new Geom_Plane(g.frame);
                else if (g.kind == K::Sphere)
                    g.surface = new Geom_SphericalSurface(g.frame, g.radius);
                else if (g.kind == K::Cylinder)
                    g.surface = new Geom_CylindricalSurface(g.frame, g.radius);
                else if (g.kind == K::Cone)
                    g.surface = new Geom_ConicalSurface(g.frame, g.semiAngle, g.radius);
                else
                    throw std::runtime_error("This surface contact pair is not supported.");
            }
            if (std::abs(g.u1 - g.u0) < 1e-12 || std::abs(g.v1 - g.v0) < 1e-12)
                throw std::runtime_error("Degenerate contact surface domain.");
            if (!initialized)
            {
                d.motion.coordinates.push_back(0.5);
                d.motion.coordinates.push_back(0.5);
            }
        }
    }
    else if (!initialized && mate.kind == MateKind::Coincident)
    {
        if (d.references[0].geometry.kind == K::Curve || d.references[1].geometry.kind == K::Curve)
            d.motion.coordinates.push_back(0.5);
        else if (d.references[0].geometry.kind == K::Surface ||
                 d.references[1].geometry.kind == K::Surface)
            d.motion.coordinates = {0.5, 0.5};
    }
    if (mate.kind == MateKind::Gear || mate.kind == MateKind::RackPinion ||
        mate.kind == MateKind::Screw || mate.kind == MateKind::UniversalJoint ||
        mate.kind == MateKind::Belt)
    {
        const auto n = mate.kind == MateKind::Belt
                           ? (d.parameters.belt.engaged ? d.references.size() : 0)
                           : std::size_t(2);
        if (!initialized)
            d.motion.coordinates.assign(n, 0);
        else
        {
            if (d.motion.coordinates.size() != n)
                throw std::runtime_error("Invalid mechanical motion state.");
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto& ref = d.references[i];
                const auto anchor = d.motion.anchors[i];
                const double actual =
                    mate.kind == MateKind::Screw && i == 1
                        ? d.motion.coordinates[0] + relativeScrewPhase(d, poses)
                        : phase(pose(ref.body, poses), anchor,
                                ref.geometry.frame.Direction().Transformed(anchor));
                d.motion.coordinates[i] =
                    actual + 2 * pi * std::round((d.motion.coordinates[i] - actual) / (2 * pi));
            }
        }
    }
    std::size_t expected = 0;
    if (mate.kind == MateKind::Tangent &&
        surfaceContact(d.references[0].geometry, d.references[1].geometry))
        expected = 4;
    if (mate.kind == MateKind::Coincident &&
        (d.references[0].geometry.kind == K::Curve || d.references[1].geometry.kind == K::Curve))
        expected = 1;
    if (mate.kind == MateKind::Coincident && (d.references[0].geometry.kind == K::Surface ||
                                              d.references[1].geometry.kind == K::Surface))
        expected = 2;
    if (mate.kind == MateKind::Gear || mate.kind == MateKind::RackPinion ||
        mate.kind == MateKind::Screw || mate.kind == MateKind::UniversalJoint)
        expected = 2;
    if (mate.kind == MateKind::Belt)
        expected = d.parameters.belt.engaged ? d.references.size() : 0;
    if (mate.kind == MateKind::Path || mate.kind == MateKind::Slot || mate.kind == MateKind::Cam)
        expected = d.motion.coordinates.size();
    if (d.motion.coordinates.size() != expected)
        throw std::runtime_error("Invalid mate auxiliary coordinate state.");
    for (double value : d.motion.coordinates)
        if (!std::isfinite(value) || std::abs(value) > 1e9)
            throw std::runtime_error("Invalid mate motion state.");
    d.motion.initialized = true;
}
MateResidual mateResidual(const ResolvedMate& mate, const std::vector<gp_Trsf>& poses,
                          const std::vector<double>& q, double scale, bool diagnose)
{
    const auto& d = *mate.definition;
    const auto& p = d.parameters;
    Eval e{{}, scale, diagnose};
    const auto get = [&](const char* name, std::size_t i = 0)
    {
        const auto refs = role(d, name);
        if (i >= refs.size())
            throw std::runtime_error(std::string("Missing mate role: ") + name);
        return World(*refs[i], poses);
    };
    const auto twist = [&](std::size_t i, double theta)
    {
        const auto& ref = d.references.at(i);
        const auto anchor = d.motion.anchors.at(i);
        const double actual = phase(pose(ref.body, poses), anchor,
                                    ref.geometry.frame.Direction().Transformed(anchor));
        e.scalar(std::sin(theta) - std::sin(actual));
        e.scalar(std::cos(theta) - std::cos(actual));
    };
    const auto concentric = [&](const World& a, const World& b)
    {
        if ((a.g.kind == K::Sphere || a.g.kind == K::Point) &&
            (b.g.kind == K::Sphere || b.g.kind == K::Point))
            e.vector(gap(a, b), true);
        else if (point(a.g))
            e.vector(gap(a, b).Crossed(b.n), true);
        else if (point(b.g))
            e.vector(gap(a, b).Crossed(a.n), true);
        else
        {
            e.align(a, b, mate.alignment);
            e.vector(gap(a, b).Crossed(a.n), true);
        }
    };
    if (mate.kind == MateKind::Symmetric)
    {
        const auto plane = get("plane"), a = get("entities", 0), b = get("entities", 1);
        if (plane.g.kind != K::Plane || a.g.kind != b.g.kind)
            throw std::runtime_error("Symmetry requires a plane and two similar entities.");
        const auto mirrored = a.c.Translated(plane.n * (-2 * gp_Vec(plane.c, a.c).Dot(plane.n)));
        if (a.g.kind == K::Plane)
        {
            e.vector(b.n - reflect(a.n, plane.n) * mate.alignment);
            e.scalar(gp_Vec(mirrored, b.c).Dot(b.n), true);
        }
        else if (axis(a.g))
        {
            e.vector(b.n - reflect(a.n, plane.n) * mate.alignment);
            e.vector(gp_Vec(mirrored, b.c).Crossed(b.n), true);
        }
        else
            e.vector(gp_Vec(mirrored, b.c), true);
        if (std::abs(a.g.radius - b.g.radius) > 1e-6)
            throw std::runtime_error(
                "Symmetric spherical/cylindrical entities require equal radii.");
        return e.result;
    }
    if (mate.kind == MateKind::Width)
        return widthResidual(mate, poses, scale, diagnose);
    if (mate.kind == MateKind::Path || mate.kind == MateKind::Slot || mate.kind == MateKind::Cam)
    {
        const bool pathMate = mate.kind == MateKind::Path, slot = mate.kind == MateKind::Slot;
        const auto pathRefs = role(d, pathMate ? "path" : slot ? "slot" : "cam");
        const auto follower = get(pathMate ? "point" : "follower");
        const World carrier(*pathRefs[0], poses);
        const auto path = pathRefs[0]->geometry.path;
        if (!path || q.empty())
            throw std::runtime_error("Mate path has no validated geometry or parameter.");
        const double s = q[0];
        auto target = path->point(s).Transformed(carrier.placement);
        gp_Vec tangent(path->tangent(s).Transformed(carrier.placement));
        if (pathMate)
        {
            e.vector(gp_Vec(target, follower.c), true);
            if (p.path.followAxis >= 0)
            {
                const gp_Vec axes[] = {gp_Vec(follower.frame.XDirection()),
                                       gp_Vec(follower.frame.YDirection()), follower.n};
                e.vector(axes[p.path.followAxis] - tangent * (p.path.flipTangent ? -1 : 1));
            }
            if (p.path.upAxis >= 0)
            {
                const auto up = get("up");
                const gp_Vec axes[] = {gp_Vec(follower.frame.XDirection()),
                                       gp_Vec(follower.frame.YDirection()), follower.n};
                auto direction = up.n;
                if (p.path.followAxis >= 0)
                {
                    direction -= tangent * direction.Dot(tangent);
                    if (direction.SquareMagnitude() < 1e-12)
                        throw std::runtime_error("The path tangent is parallel to its up "
                                                 "reference. Choose another up vector.");
                    direction.Normalize();
                }
                e.vector(axes[p.path.upAxis] - direction * (p.path.flipUp ? -1 : 1));
            }
        }
        else
        {
            const gp_Vec normal(
                carrier.g.profile == MateProfileKind::Slot
                    ? carrier.g.profileFrame.Direction().Transformed(carrier.placement)
                    : carrier.frame.Direction());
            if (slot)
            {
                const auto other =
                    follower.g.profile == MateProfileKind::Slot && q.size() > 1
                        ? follower.g.path->point(q[1]).Transformed(follower.placement)
                        : follower.c;
                e.vector(gp_Vec(target, other).Crossed(normal), true);
                e.vector(follower.n - normal * mate.alignment);
            }
            else
            {
                const auto outward = tangent.Crossed(normal).Normalized();
                if (follower.g.kind == K::Plane)
                {
                    e.vector(follower.n - outward * mate.alignment);
                    e.scalar(gp_Vec(target, follower.c).Dot(follower.n), true);
                }
                else
                {
                    target.Translate(outward *
                                     (follower.g.kind == K::Cylinder ? follower.g.radius : 0) *
                                     (p.standard.internalTangency ? -1 : 1));
                    e.vector(gp_Vec(target, follower.c).Crossed(normal), true);
                    if (follower.g.kind == K::Cylinder)
                        e.vector(follower.n - normal * mate.alignment);
                }
            }
        }
        if (!path->closed)
            e.bound(s, 0, 1);
        if (mate.kind != MateKind::Cam && p.position.mode == PositionMode::Centered)
            e.scalar(s - 0.5);
        else if (mate.kind != MateKind::Cam && p.position.mode != PositionMode::Free)
        {
            double targetFraction = p.position.mode == PositionMode::Distance
                                        ? p.position.distance / path->length
                                        : p.position.percent / 100;
            if (p.position.flip)
                targetFraction = 1 - targetFraction;
            e.scalar(s - targetFraction);
        }
        if (slot && q.size() > 1)
        {
            e.bound(q[1], 0, 1);
            if (p.position.mode != PositionMode::Free)
                e.scalar(q[1] - 0.5);
        }
        if (slot && p.standard.lockRotation)
        {
            const auto followerRef = role(d, "follower")[0];
            const auto& base = d.motion.anchors.front();
            const auto idx = std::size_t(followerRef - &d.references[0]);
            const auto relative =
                carrier.placement.GetRotation().Inverted() * follower.placement.GetRotation();
            const auto original =
                base.GetRotation().Inverted() * d.motion.anchors[idx].GetRotation();
            const auto rotation = relative * original.Inverted();
            e.scalar(gp_Vec(rotation.X(), rotation.Y(), rotation.Z())
                         .Dot(gp_Vec(carrier.g.profileFrame.Direction())));
        }
        return e.result;
    }
    if (mate.kind == MateKind::Hinge)
    {
        const auto a = get("concentric", 0), b = get("concentric", 1), c = get("coincident", 0),
                   f = get("coincident", 1);
        concentric(a, b);
        e.scalar(gap(c, f).Dot(c.n), true);
        if (p.hinge.limits)
            e.bound(angle(get("angle", 0), get("angle", 1)) * 180 / pi, p.limit.minimum,
                    p.limit.maximum);
        return e.result;
    }
    const auto a = get(mate.kind == MateKind::RackPinion ? "rack"
                       : mate.kind == MateKind::Belt     ? "pulleys"
                                                         : "first",
                       0),
               b = get(mate.kind == MateKind::RackPinion ? "pinion"
                       : mate.kind == MateKind::Belt     ? "pulleys"
                                                         : "second",
                       mate.kind == MateKind::Belt ? 1 : 0);
    switch (mate.kind)
    {
    case MateKind::Coincident:
        if (a.g.kind == K::Surface || b.g.kind == K::Surface)
        {
            const auto& surface = a.g.kind == K::Surface ? a : b;
            const auto& other = a.g.kind == K::Surface ? b : a;
            const auto target = surfacePoint(surface, q.at(0), q.at(1));
            e.vector(gp_Vec(target, other.c), true);
            e.bound(q[0], 0, 1);
            e.bound(q[1], 0, 1);
        }
        else if (a.g.kind == K::Curve || b.g.kind == K::Curve)
        {
            const auto& curve = a.g.kind == K::Curve ? a : b;
            const auto& other = a.g.kind == K::Curve ? b : a;
            const auto at =
                curve.g.curve->Value(curve.g.first + q.at(0) * (curve.g.last - curve.g.first))
                    .Transformed(curve.placement);
            e.vector(gp_Vec(at, other.c), true);
            e.bound(q[0], 0, 1);
        }
        else
            e.coincident(a, b, mate.alignment);
        if (p.standard.alignAxes)
        {
            e.vector(b.n - a.n * mate.alignment);
            e.vector(b.x - a.x);
        }
        break;
    case MateKind::Concentric:
        concentric(a, b);
        break;
    case MateKind::Parallel:
        if ((a.g.kind == K::Plane) != (b.g.kind == K::Plane))
            e.scalar(a.n.Dot(b.n));
        else
            e.align(a, b, mate.alignment);
        break;
    case MateKind::Perpendicular:
        if ((a.g.kind == K::Plane) != (b.g.kind == K::Plane))
            e.vector(b.n - a.n * mate.alignment);
        else
            e.scalar(a.n.Dot(b.n));
        break;
    case MateKind::Angle:
    case MateKind::LimitAngle:
    {
        const double v = angle(a, b) * 180 / pi;
        if (mate.kind == MateKind::LimitAngle)
            e.bound(v, p.limit.minimum, p.limit.maximum);
        else if (mate.value < 1e-8 || 180 - mate.value < 1e-8)
            e.vector(b.n - a.n * (mate.value < 90 ? 1 : -1));
        else
            e.scalar((v - mate.value) * pi / 180);
        break;
    }
    case MateKind::Distance:
    case MateKind::LimitDistance:
    {
        if (a.g.kind == K::Plane && b.g.kind == K::Plane)
            e.align(a, b, mate.alignment);
        double v = distance(a, b);
        const auto arc = [](ArcCondition c, double radius)
        {
            return c == ArcCondition::Minimum ? -radius : c == ArcCondition::Maximum ? radius : 0;
        };
        v += arc(p.standard.firstArc, a.g.radius) + arc(p.standard.secondArc, b.g.radius);
        if (mate.kind == MateKind::LimitDistance)
            e.bound(v, p.limit.minimum, p.limit.maximum, true);
        else
            e.scalar(v - mate.value, true);
        break;
    }
    case MateKind::Tangent:
    {
        if (surfaceContact(a.g, b.g))
        {
            gp_Vec na, nb;
            const auto pa = surfacePoint(a, q.at(0), q.at(1), &na),
                       pb = surfacePoint(b, q.at(2), q.at(3), &nb);
            e.vector(gp_Vec(pa, pb), true);
            e.vector(nb - na * mate.alignment);
            for (std::size_t i = 0; i < 2; ++i)
            {
                const auto& g = d.references[i].geometry;
                if (g.shape.IsNull())
                    continue;
                const bool uPeriodic =
                    g.surface->IsUPeriodic() && std::abs(g.u1 - g.u0 - g.surface->UPeriod()) < 1e-6;
                const bool vPeriodic =
                    g.surface->IsVPeriodic() && std::abs(g.v1 - g.v0 - g.surface->VPeriod()) < 1e-6;
                if (!uPeriodic)
                    e.bound(q[2 * i], 0, 1);
                if (!vPeriodic)
                    e.bound(q[2 * i + 1], 0, 1);
            }
        }
        else
        {
            const bool ac = a.g.kind == K::Cylinder, bc = b.g.kind == K::Cylinder;
            const double radii = p.standard.internalTangency ? std::abs(a.g.radius - b.g.radius)
                                                             : a.g.radius + b.g.radius;
            if (a.g.kind == K::Plane)
            {
                if (bc)
                    e.scalar(a.n.Dot(b.n));
                e.scalar(std::abs(gap(a, b).Dot(a.n)) - b.g.radius, true);
            }
            else if (b.g.kind == K::Plane)
            {
                if (ac)
                    e.scalar(a.n.Dot(b.n));
                e.scalar(std::abs(gap(a, b).Dot(b.n)) - a.g.radius, true);
            }
            else
                e.scalar(distance(a, b) - radii, true);
        }
        break;
    }
    case MateKind::Lock:
    case MateKind::InPlace:
    case MateKind::LockToSketch:
    {
        const auto currentA = pose(d.references[0].body, poses),
                   currentB = pose(d.references[1].body, poses);
        const auto expected = currentA * d.motion.anchors[0].Inverted() * d.motion.anchors[1];
        e.vector(gp_Vec(expected.TranslationPart(), currentB.TranslationPart()), true);
        const auto rotation = currentB.GetRotation() * expected.GetRotation().Inverted();
        e.vector(gp_Vec(rotation.X(), rotation.Y(), rotation.Z()) * (rotation.W() < 0 ? -2 : 2));
        break;
    }
    case MateKind::ProfileCenter:
    {
        auto af = a.g.profileFrame, bf = b.g.profileFrame;
        af.Transform(a.placement);
        bf.Transform(b.placement);
        e.vector(gp_Vec(af.Location().Translated(gp_Vec(af.Direction()) * p.profile.offset),
                        bf.Location()),
                 true);
        e.vector(gp_Vec(bf.Direction()) - gp_Vec(af.Direction()) * mate.alignment);
        if (a.g.profile != MateProfileKind::Circle && b.g.profile != MateProfileKind::Circle)
        {
            gp_Vec expected(af.XDirection());
            expected.Rotate(gp_Ax1(af.Location(), af.Direction()),
                            p.profile.rotation * pi / 180 * (p.profile.clockwise ? -1 : 1));
            e.vector(gp_Vec(bf.XDirection()) - expected);
        }
        break;
    }
    case MateKind::LinearCoupler:
    {
        double values[2]{};
        for (int i = 0; i < 2; ++i)
        {
            const auto& ref = d.references[std::size_t(i)];
            gp_Trsf reference;
            const auto base = role(d, i == 0 ? "first-reference" : "second-reference");
            gp_Trsf anchorReference;
            if (!base.empty())
            {
                reference = pose(base[0]->body, poses);
                const auto idx = std::size_t(base[0] - &d.references[0]);
                anchorReference = d.motion.anchors[idx];
            }
            const auto current = reference.Inverted() * pose(ref.body, poses),
                       anchor = anchorReference.Inverted() * d.motion.anchors[std::size_t(i)];
            const auto direction = ref.geometry.frame.Direction().Transformed(anchor);
            values[i] =
                gp_Vec(anchor.TranslationPart(), current.TranslationPart()).Dot(gp_Vec(direction));
        }
        e.scalar(values[0] * p.coupler.secondRatio +
                     (p.coupler.reverse ? 1 : -1) * values[1] * p.coupler.firstRatio,
                 true);
        break;
    }
    case MateKind::Gear:
    case MateKind::Belt:
    case MateKind::RackPinion:
    case MateKind::Screw:
    case MateKind::UniversalJoint:
    {
        for (std::size_t i = 0; i < q.size(); ++i)
        {
            if (mate.kind == MateKind::Screw && i == 1)
            {
                const double relative = relativeScrewPhase(d, poses);
                e.scalar(std::sin(q[1] - q[0]) - std::sin(relative));
                e.scalar(std::cos(q[1] - q[0]) - std::cos(relative));
            }
            else
                twist(i, q[i]);
        }
        const double sign = p.coupler.reverse ? -1 : 1;
        if (mate.kind == MateKind::Gear)
        {
            if (p.coupler.useGeometryRatio && (a.g.radius <= 1e-6 || b.g.radius <= 1e-6))
                throw std::runtime_error(
                    "A geometry-derived gear ratio needs two circular edges or cylindrical faces.");
            e.scalar(q.at(0) * (p.coupler.useGeometryRatio ? a.g.radius : p.coupler.firstRatio) +
                     sign * q.at(1) *
                         (p.coupler.useGeometryRatio ? b.g.radius : p.coupler.secondRatio));
        }
        else if (mate.kind == MateKind::Belt)
        {
            const auto radius = [&](std::size_t i)
            {
                const double r = p.belt.diameters.empty() || p.belt.diameters.at(i) == 0
                                     ? d.references[i].geometry.radius
                                     : p.belt.diameters[i] / 2;
                if (r <= 1e-6)
                    throw std::runtime_error(
                        "A pulley axis requires a positive diameter override.");
                return r;
            };
            const auto side = [&](std::size_t i)
            { return p.belt.sides.empty() ? 1 : p.belt.sides.at(i); };
            for (std::size_t i = 1; i < q.size(); ++i)
            {
                const auto firstNormal = d.references[0].geometry.frame.Direction().Transformed(
                               d.motion.anchors[0]),
                           normal = d.references[i].geometry.frame.Direction().Transformed(
                               d.motion.anchors[i]);
                e.scalar(q[0] * radius(0) * side(0) - sign * q[i] * radius(i) * side(i) *
                                                          (firstNormal.Dot(normal) < 0 ? -1 : 1));
            }
            if (p.belt.driving)
            {
                std::vector<BeltPulley> pulleys;
                for (std::size_t i = 0; i < d.references.size(); ++i)
                {
                    World pulley(d.references[i], poses);
                    auto frame = pulley.frame;
                    frame.SetDirection(a.frame.Direction());
                    pulleys.push_back({frame, radius(i) + p.belt.thickness / 2, side(i)});
                }
                e.scalar(beltLength(pulleys) - p.belt.length, true);
            }
            if (!p.belt.engaged && !p.belt.driving)
                e.scalar(0);
        }
        else if (mate.kind == MateKind::RackPinion)
        {
            const auto current = pose(d.references[0].body, poses), anchor = d.motion.anchors[0];
            const double travel =
                gp_Vec(anchor.TranslationPart(), current.TranslationPart()).Dot(a.n);
            e.scalar(travel - sign * q.at(1) * p.coupler.pitch / (2 * pi), true);
        }
        else if (mate.kind == MateKind::Screw)
        {
            concentric(a, b);
            const auto anchorA = a.g.frame.Location().Transformed(d.motion.anchors[0]),
                       anchorB = b.g.frame.Location().Transformed(d.motion.anchors[1]);
            const double initial = gp_Vec(anchorA, anchorB)
                                       .Dot(a.g.frame.Direction().Transformed(d.motion.anchors[0]));
            e.scalar(gap(a, b).Dot(a.n) - initial -
                         sign * (q.at(1) - q.at(0)) * p.coupler.pitch / (2 * pi),
                     true);
        }
        else
            e.scalar(std::sin(q.at(1)) * std::cos(q.at(0)) -
                     sign * a.n.Dot(b.n) * std::cos(q.at(1)) * std::sin(q.at(0)));
        break;
    }
    case MateKind::Magnetic:
        e.vector(gap(a, b), true);
        e.vector(b.n + a.n);
        e.vector(b.x - a.x);
        break;
    default:
        throw std::runtime_error("Mate definition has no equation implementation.");
    }
    if (p.standard.lockRotation &&
        (mate.kind == MateKind::Concentric ||
         (mate.kind == MateKind::ProfileCenter &&
          (a.g.profile == MateProfileKind::Circle || b.g.profile == MateProfileKind::Circle)) ||
         mate.kind == MateKind::Slot))
    {
        const auto qa = pose(d.references[0].body, poses), qb = pose(d.references[1].body, poses);
        const auto relative = qa.GetRotation().Inverted() * qb.GetRotation();
        const auto original =
            d.motion.anchors[0].GetRotation().Inverted() * d.motion.anchors[1].GetRotation();
        const auto delta = relative * original.Inverted();
        e.scalar(gp_Vec(delta.X(), delta.Y(), delta.Z()).Dot(gp_Vec(a.g.frame.Direction())));
    }
    return e.result;
}
void validateMateSolution(const ResolvedMate& mate, const std::vector<double>& q)
{
    if (!mate.definition)
        return;
    for (double v : q)
        if (!std::isfinite(v) || std::abs(v) > 1e9)
            throw std::runtime_error("Invalid mate motion state.");
    const auto& d = *mate.definition;
    const auto checkSurface = [](const MateGeometry& g, double u, double v)
    {
        if (g.shape.IsNull() || g.shape.ShapeType() != TopAbs_FACE)
            return;
        if (g.surface->IsUPeriodic() && std::abs(g.u1 - g.u0 - g.surface->UPeriod()) < 1e-6)
            u -= std::floor(u);
        if (g.surface->IsVPeriodic() && std::abs(g.v1 - g.v0 - g.surface->VPeriod()) < 1e-6)
            v -= std::floor(v);
        const gp_Pnt2d uv(g.u0 + u * (g.u1 - g.u0), g.v0 + v * (g.v1 - g.v0));
        const BRepClass_FaceClassifier classifier(TopoDS::Face(g.shape), uv, 1e-6);
        if (classifier.State() != TopAbs_IN && classifier.State() != TopAbs_ON)
            throw std::runtime_error("Mate contact falls outside the selected trimmed face. Choose "
                                     "another reference or contact branch.");
    };
    if (mate.kind == MateKind::Tangent &&
        surfaceContact(d.references[0].geometry, d.references[1].geometry))
        for (std::size_t i = 0; i < 2; ++i)
            checkSurface(d.references[i].geometry, q.at(2 * i), q.at(2 * i + 1));
    if (mate.kind == MateKind::Coincident && q.size() == 2)
        for (const auto& ref : d.references)
            if (ref.geometry.kind == K::Surface)
                checkSurface(ref.geometry, q[0], q[1]);
}
} // namespace thecad::modeling::assembly
