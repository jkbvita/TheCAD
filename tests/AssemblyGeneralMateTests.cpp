#include "modeling/assembly/MateEquations.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopExp_Explorer.hxx>
#include <Geom_Line.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Lin.hxx>
#include <gp_Circ.hxx>
#include <iostream>
#include <cmath>
#include <stdexcept>
using namespace thecad::modeling::assembly;
namespace
{
constexpr double pi = 3.14159265358979323846;
using K = MateGeometryKind;
void require(bool value, const std::string& message)
{
    if (!value)
        throw std::runtime_error(message);
}
MateGeometry entity(K kind, gp_Pnt p = {}, gp_Dir n = {0, 0, 1}, double radius = 0)
{
    MateGeometry g;
    g.kind = kind;
    g.frame = gp_Ax3(p, n);
    g.radius = radius;
    return g;
}
ResolvedMate pair(MateKind kind, MateGeometry a, MateGeometry b)
{
    ResolvedMate m;
    m.id = mateName(kind);
    m.kind = kind;
    m.first = 0;
    m.second = 1;
    m.definition = ResolvedMateDefinition{};
    m.definition->references = {{"first", 0, a}, {"second", 1, b}};
    return m;
}
std::vector<RigidBody> bodies(gp_Vec translation = {4, 3, 8}, double rotation = 0.2)
{
    gp_Trsf t;
    t.SetRotation(gp_Quaternion(gp_Vec(1, 1, 1), rotation));
    t.SetTranslationPart(translation);
    return {{"fixed", {}, true, 10}, {"moving", t, false, 10}};
}
MateSolution solve(const std::vector<RigidBody>& b, ResolvedMate m, int freedom)
{
    auto s = solveMates(b, {m});
    require(s.degreesOfFreedom == freedom, std::string(mateName(m.kind)) +
                                               " freedom: " + std::to_string(s.degreesOfFreedom) +
                                               " expected " + std::to_string(freedom));
    m.definition->motion = s.motion[0];
    prepareMate(m, s.placements);
    const auto r = mateResidual(m, s.placements, s.motion[0].coordinates, 10);
    for (std::size_t i = 0; i < r.values.size(); ++i)
        require(std::abs(r.values[i]) <= r.tolerances[i] * 1.01,
                std::string(mateName(m.kind)) + " residual failed");
    return s;
}
void standards()
{
    const auto plane = entity(K::Plane), axis = entity(K::Line), point = entity(K::Point),
               sphere = entity(K::Sphere, {}, gp_Dir(0, 0, 1), 2),
               cylinder = entity(K::Cylinder, {}, gp_Dir(0, 0, 1), 2);
    solve(bodies(), pair(MateKind::Coincident, plane, plane), 3);
    solve(bodies(), pair(MateKind::Coincident, point, point), 3);
    solve(bodies(), pair(MateKind::Coincident, point, plane), 5);
    solve(bodies(), pair(MateKind::Coincident, axis, cylinder), 3);
    solve(bodies(), pair(MateKind::Concentric, cylinder, cylinder), 2);
    solve(bodies(), pair(MateKind::Concentric, axis, sphere), 4);
    solve(bodies(), pair(MateKind::Parallel, plane, plane), 4);
    solve(bodies(), pair(MateKind::Perpendicular, plane, plane), 5);
    solve(bodies(), pair(MateKind::Tangent, plane, cylinder), 4);
    solve(bodies(), pair(MateKind::Tangent, plane, sphere), 5);
    auto m = pair(MateKind::Distance, plane, plane);
    m.value = 7;
    auto s = solve(bodies(), m, 3);
    require(std::abs(s.placements[1].TranslationPart().Z() - 7) < 1e-5, "Plane distance incorrect");
    m = pair(MateKind::Angle, axis, axis);
    m.value = 37;
    solve(bodies(), m, 5);
    for (double angle : {0., 90., 180.})
    {
        m.value = angle;
        solve(bodies({}, 0), m, angle == 90 ? 5 : 4);
    }
    m = pair(MateKind::Lock, point, point);
    auto b = bodies();
    b[0].fixed = false;
    solve(b, m, 6);
    m = pair(MateKind::Concentric, cylinder, cylinder);
    m.definition->parameters.standard.lockRotation = true;
    solve(bodies(), m, 1);
    require(!compatibleMate(MateKind::Coincident, plane, sphere),
            "Plane/sphere coincident is invalid");
    require(!compatibleMate(MateKind::Angle, plane, axis), "Line/plane angle accepted");
    require(!compatibleMate(MateKind::Concentric, plane, cylinder),
            "Plane/cylinder concentric accepted");
}
void advanced()
{
    const auto plane = entity(K::Plane), axis = entity(K::Line), point = entity(K::Point);
    auto m = pair(MateKind::LimitDistance, plane, plane);
    m.definition->parameters.limit = {2, 5};
    solve(bodies({0, 0, 12}, 0), m, 4); // only parallel orientation constrained inside/at limits
    m = pair(MateKind::LimitAngle, axis, axis);
    m.definition->parameters.limit = {10, 30};
    solve(bodies({0, 0, 0}, 0.9), m, 6);
    auto profile = plane;
    profile.profile = MateProfileKind::Rectangle;
    profile.profileSides = 4;
    profile.profileWidth = 10;
    profile.profileHeight = 5;
    m = pair(MateKind::ProfileCenter, profile, profile);
    solve(bodies(), m, 0);
    m = pair(MateKind::Symmetric, point, point);
    m.definition->references = {
        {"plane", 0, plane}, {"entities", 0, entity(K::Point, {2, 4, 5})}, {"entities", 1, point}};
    auto s = solve(bodies(), m, 3);
    require(gp_Pnt(2, 4, -5).Distance(gp_Pnt().Transformed(s.placements[1])) < 1e-5,
            "Point symmetry is wrong");
    m = pair(MateKind::Width, plane, plane);
    m.definition->references = {{"width", 0, entity(K::Plane, {0, 0, -5})},
                                {"width", 0, entity(K::Plane, {0, 0, 5})},
                                {"tab", 1, entity(K::Plane, {0, 0, -1})},
                                {"tab", 1, entity(K::Plane, {0, 0, 1})}};
    solve(bodies(), m, 3);
    m.definition->parameters.position.mode = PositionMode::Free;
    solve(bodies(), m, 4);
    m.definition->parameters.position.mode = PositionMode::Percent;
    m.definition->parameters.position.percent = 25;
    auto widthResult = solve(bodies(), m, 3);
    require(std::abs(widthResult.placements[1].TranslationPart().Z() + 2) < 1e-5,
            "Width percent position is incorrect");
    m.definition->references = {{"width", 0, entity(K::Plane, {0, 0, -5})},
                                {"width", 0, entity(K::Plane, {0, 0, 5})},
                                {"tab", 1, entity(K::Cylinder, {}, gp_Dir(1, 0, 0), 1)}};
    m.definition->parameters.position.mode = PositionMode::Centered;
    solve(bodies(), m, 4);
    m.definition->references = {{"width", 0, entity(K::Plane, {}, gp_Dir(0, 1, 5))},
                                {"width", 0, entity(K::Plane, {}, gp_Dir(0, -1, 5))},
                                {"tab", 1, entity(K::Plane, {0, 0, -1})},
                                {"tab", 1, entity(K::Plane, {0, 0, 1})}};
    solve(bodies(), m, 3);
    MateGeometry path = entity(K::Line);
    path.curve = new Geom_Line(gp_Lin(gp_Pnt(), gp_Dir(1, 0, 0)));
    path.first = 0;
    path.last = 20;
    m = pair(MateKind::Path, point, path);
    m.definition->parameters.position.mode = PositionMode::Free;
    m.definition->references = {{"point", 1, point}, {"path", 0, path}};
    solve(bodies(), m, 4);
    m.definition->parameters.position.mode = PositionMode::Percent;
    m.definition->parameters.position.percent = 25;
    s = solve(bodies(), m, 3);
    require(gp_Pnt(5, 0, 0).Distance(gp_Pnt().Transformed(s.placements[1])) < 1e-5,
            "Percent path placement failed");
    m.definition->motion = s.motion[0]; // warm solve must rebuild derived path
    auto b = bodies();
    b[1].placement = s.placements[1];
    solve(b, m, 3);
    m = pair(MateKind::LinearCoupler, axis, axis);
    b = bodies({}, 0);
    b[0].fixed = false;
    solve(b, m, 11);
}
void mechanical()
{
    const auto axis = entity(K::Line), plane = entity(K::Plane),
               cylinder = entity(K::Cylinder, {}, gp_Dir(0, 0, 1), 2);
    auto m = pair(MateKind::Hinge, axis, axis);
    m.definition->references = {{"concentric", 0, axis},
                                {"concentric", 1, axis},
                                {"coincident", 0, plane},
                                {"coincident", 1, plane}};
    solve(bodies(), m, 1);
    m = pair(MateKind::Gear, axis, axis);
    auto b = bodies({}, 0);
    b[0].fixed = false;
    solve(b, m, 11);
    m = pair(MateKind::RackPinion, axis, axis);
    m.definition->references[0].role = "rack";
    m.definition->references[1].role = "pinion";
    solve(b, m, 11);
    m = pair(MateKind::Screw, cylinder, cylinder);
    solve(bodies(), m, 1);
    m = pair(MateKind::UniversalJoint, axis, entity(K::Line, {}, gp_Dir(1, 0, 1)));
    solve(b, m, 11);
    auto slot = plane;
    slot.profile = MateProfileKind::Slot;
    MateGeometry line = axis;
    line.curve = new Geom_Line(gp_Lin(gp_Pnt(), gp_Dir(1, 0, 0)));
    line.first = 0;
    line.last = 10;
    slot.path = matePath({line});
    m = pair(MateKind::Slot, slot, cylinder);
    m.definition->references[0].role = "slot";
    m.definition->references[1].role = "follower";
    m.definition->parameters.position.mode = PositionMode::Free;
    solve(bodies(), m, 3);
    m.definition->parameters.position.mode = PositionMode::Centered;
    solve(bodies(), m, 2);
    MateGeometry cam;
    const auto camSolid = BRepPrimAPI_MakeCylinder(5, 10).Shape();
    for (TopExp_Explorer it(camSolid, TopAbs_FACE); it.More(); it.Next())
    {
        auto face = mateGeometry(it.Current());
        if (face.kind == K::Cylinder)
        {
            cam = face;
            break;
        }
    }
    m = pair(MateKind::Cam, cam, cylinder);
    m.definition->references[0].role = "cam";
    m.definition->references[1].role = "follower";
    m.definition->parameters.position.mode = PositionMode::Free;
    solve(bodies(), m, 3);
    m = pair(MateKind::Belt, cylinder, cylinder);
    m.definition->references[0].role = m.definition->references[1].role = "pulleys";
    solve(b, m, 11);
    const auto frame = entity(K::Frame);
    m = pair(MateKind::Magnetic, frame, frame);
    solve(bodies(), m, 0);
    for (auto kind : {MateKind::LockToSketch, MateKind::InPlace})
        solve(bodies(), pair(kind, frame, frame), 0);
}
void geometry()
{
    BRepBuilderAPI_MakePolygon polygon;
    for (auto p : {gp_Pnt(-4, -2, 0), gp_Pnt(4, -2, 0), gp_Pnt(4, 2, 0), gp_Pnt(-4, 2, 0)})
        polygon.Add(p);
    polygon.Close();
    const auto face = BRepBuilderAPI_MakeFace(polygon.Wire()).Face();
    auto g = mateGeometry(face);
    require(g.profile == MateProfileKind::Rectangle &&
                g.profileFrame.Location().Distance(gp_Pnt()) < 1e-6,
            "Rectangle center extraction incorrect");
    const auto circle =
        mateGeometry(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2({}, gp_Dir(0, 0, 1)), 4)).Edge());
    auto path = matePath({circle});
    require(path->closed && std::abs(path->length - 8 * pi) < 1e-6,
            "Circular path length is wrong");
    require(path->point(0.25).Distance(gp_Pnt(0, 4, 0)) < 1e-6, "Circular path position is wrong");
}
} // namespace
int main()
{
    try
    {
        standards();
        advanced();
        mechanical();
        geometry();
        std::cout << "PASS: typed standard, advanced, mechanical and generated equations, freedom "
                     "and geometry\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
