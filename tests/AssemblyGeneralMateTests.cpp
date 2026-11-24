#include "modeling/assembly/MateEquations.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
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
void constrainedDragging()
{
    const auto cylinder = entity(K::Cylinder, {}, gp_Dir(0, 0, 1), 2);
    const auto plane = entity(K::Plane);
    auto assembly = bodies({}, 0);
    const std::vector<ResolvedMate> mates = {
        pair(MateKind::Concentric, cylinder, cylinder),
        pair(MateKind::Coincident, plane, plane)};
    const gp_Pnt grab(4, 0, 0), target(0, 4, 0);
    const auto result = dragMates(assembly, mates, {1, grab, target, gp_Dir(0, 0, 1), 0.01});
    const double dragError = grab.Transformed(result.placements[1]).Distance(target);
    require(dragError < 0.1,
            "Dragging an off-axis point must rotate a concentric, seated component; error=" +
                std::to_string(dragError));
    require(result.placements[1].TranslationPart().Modulus() < 1e-4,
            "Concentric drag must keep the shaft seated on its axis");
    require(result.placements[0].TranslationPart().Modulus() < 1e-12,
            "Dragging must not move the fixed component");

    auto movingBodies = assembly;
    auto movingMates = mates;
    for (int step = 1; step <= 16; ++step)
    {
        const double angle = (step <= 8 ? step : 16 - step) * pi / 16;
        const gp_Pnt cursor(4 * std::cos(angle), 4 * std::sin(angle), 0);
        const auto preview = dragMates(
            movingBodies, movingMates, {1, grab, cursor, gp_Dir(0, 0, 1), 0.01});
        require(grab.Transformed(preview.placements[1]).Distance(cursor) < 0.1,
                "Repeated dragging must follow the same grabbed point through mate freedom");
        movingBodies[1].placement = preview.placements[1];
        for (std::size_t i = 0; i < movingMates.size(); ++i)
            movingMates[i].definition->motion = preview.motion[i];
    }
    require(grab.Transformed(movingBodies[1].placement).Distance(grab) < 0.1,
            "Returning the cursor must return the mated component without drift");

    auto locked = mates;
    locked.front().definition->parameters.standard.lockRotation = true;
    const auto blocked = dragMates(assembly, locked, {1, grab, target, gp_Dir(0, 0, 1), 0.01});
    require(grab.Transformed(blocked.placements[1]).Distance(grab) < 1e-4,
            "Lock rotation must prevent point dragging around the axis");

    const auto free = dragMates(assembly, {}, {1, grab, target, gp_Dir(0, 0, 1), 0.01});
    require(grab.Transformed(free.placements[1]).Distance(target) < 1e-4,
            "Unmated component must follow a dragged point by translation");
}
void constrainedRotation()
{
    auto assembly = bodies({}, 0);
    const auto cylinder = entity(K::Cylinder, {}, gp_Dir(0, 0, 1), 2);
    const auto plane = entity(K::Plane);
    auto mates = std::vector<ResolvedMate>{pair(MateKind::Concentric, cylinder, cylinder),
                                          pair(MateKind::Coincident, plane, plane)};
    const gp_Quaternion quarterTurn(gp_Vec(0, 0, 1), pi / 2);
    const gp_Pnt pivot(4, 2, 3);
    ComponentDragTarget target{1, pivot, {}, gp_Dir(0, 0, 1), 1e-4, quarterTurn};
    const auto free = dragMates(assembly, {}, target);
    require(pivot.Transformed(free.placements[1]).Distance(pivot) < 1e-6,
            "Free rotation must preserve its center");
    require(std::abs((free.placements[1].GetRotation() * quarterTurn.Inverted()).GetRotationAngle()) < 1e-5,
            "Free rotation must reach the requested orientation");
    const auto seated = dragMates(assembly, mates, target);
    require(std::abs((seated.placements[1].GetRotation() * quarterTurn.Inverted()).GetRotationAngle()) < 1e-3,
            "Seated concentric component must rotate around its axis");
    require(seated.placements[1].TranslationPart().Modulus() < 1e-4,
            "Rotation must preserve the axis and seat even when the preferred pivot is off-axis");
    target.rotation = gp_Quaternion(gp_Vec(0, 0, 1), 3.0);
    const auto large = dragMates(assembly, mates, target);
    require(std::abs((large.placements[1].GetRotation() * target.rotation->Inverted()).GetRotationAngle()) < 1e-3,
            "Large cursor steps must still reach the permitted orientation");
    target.rotation = quarterTurn;
    mates[0].definition->parameters.standard.lockRotation = true;
    mates.pop_back(); // Axial sliding remains possible, but rotation is blocked.
    const auto locked = dragMates(assembly, mates, target);
    require(std::abs(locked.placements[1].GetRotation().GetRotationAngle()) < 1e-5 &&
                locked.placements[1].TranslationPart().Modulus() < 1e-5,
            "Blocked rotation must not turn into unwanted sliding");
    target.rotation = gp_Quaternion(0, 0, 0, 0);
    bool rejected = false;
    try { (void)dragMates(assembly, {}, target); }
    catch (const std::exception&) { rejected = true; }
    require(rejected, "A zero rotation quaternion must be rejected");

    auto linked = bodies({10, 0, 0}, 0);
    linked[0].fixed = false;
    auto rigid = pair(MateKind::Lock, entity(K::Frame), entity(K::Frame));
    target.rotation = quarterTurn;
    const auto group = dragMates(linked, {rigid}, target);
    require(std::abs((group.placements[0].GetRotation() * quarterTurn.Inverted()).GetRotationAngle()) < 1e-3,
            "A rigidly mated floating neighbor must rotate with the dragged component");
    const auto relative = group.placements[0].Inverted() * group.placements[1];
    require((relative.TranslationPart() - gp_XYZ(10, 0, 0)).Modulus() < 1e-4,
            "Rotating linked components must preserve their relative placement");
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
void relativeMechanicalMotion()
{
    const auto cylinder = entity(K::Cylinder, {}, gp_Dir(0, 0, 1), 2);
    auto screw = pair(MateKind::Screw, cylinder, cylinder);
    screw.definition->parameters.coupler.pitch = 4;
    std::vector<gp_Trsf> poses(2);
    prepareMate(screw, poses);
    for (int step = 1; step <= 80; ++step)
    {
        const double angle = step * 0.2;
        gp_Trsf common, relative;
        common.SetRotation(gp_Quaternion(gp_Vec(1, 2, 3), step * 0.01));
        common.SetTranslationPart({step * 0.3, 2, 7});
        relative.SetRotation(gp_Quaternion(gp_Vec(0, 0, 1), angle));
        relative.SetTranslationPart({0, 0, 4 * angle / (2 * pi)});
        poses = {common, common * relative};
        prepareMate(screw, poses);
        const auto residual = mateResidual(screw, poses, screw.definition->motion.coordinates, 10);
        for (std::size_t i = 0; i < residual.values.size(); ++i)
            require(std::abs(residual.values[i]) <= residual.tolerances[i] * 1.01,
                    "Screw pitch changed under common rigid motion or a complete turn");
    }
    auto gear = pair(MateKind::Gear, cylinder, entity(K::Cylinder, {}, gp_Dir(0, 0, 1), 4));
    gear.definition->parameters.coupler.useGeometryRatio = true;
    poses.assign(2, {});
    prepareMate(gear, poses);
    poses[0].SetRotation(gp_Quaternion(gp_Vec(0, 0, 1), 0.8));
    poses[1].SetRotation(gp_Quaternion(gp_Vec(0, 0, 1), -0.4));
    prepareMate(gear, poses);
    const auto residual = mateResidual(gear, poses, gear.definition->motion.coordinates, 10);
    for (std::size_t i = 0; i < residual.values.size(); ++i)
        require(std::abs(residual.values[i]) <= residual.tolerances[i] * 1.01,
                "Geometry-derived gear ratio is wrong");
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
    BRepBuilderAPI_MakeWire capsule;
    capsule.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(-5, -2, 0), gp_Pnt(5, -2, 0)).Edge());
    capsule.Add(BRepBuilderAPI_MakeEdge(
                    GC_MakeArcOfCircle(gp_Pnt(5, -2, 0), gp_Pnt(7, 0, 0), gp_Pnt(5, 2, 0)).Value())
                    .Edge());
    capsule.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(5, 2, 0), gp_Pnt(-5, 2, 0)).Edge());
    capsule.Add(
        BRepBuilderAPI_MakeEdge(
            GC_MakeArcOfCircle(gp_Pnt(-5, 2, 0), gp_Pnt(-7, 0, 0), gp_Pnt(-5, -2, 0)).Value())
            .Edge());
    const auto camSolid =
        BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(capsule.Wire()).Face(), gp_Vec(0, 0, 5))
            .Shape();
    std::vector<MateGeometry> faces;
    for (TopExp_Explorer it(camSolid, TopAbs_FACE); it.More(); it.Next())
        faces.push_back(mateGeometry(it.Current()));
    for (std::size_t seed = 0; seed < faces.size(); ++seed)
    {
        if (faces[seed].kind == K::Plane && std::abs(faces[seed].frame.Direction().Z()) > 0.9)
            continue;
        const auto indices = camFaceChain(faces, seed);
        require(indices.size() == 4,
                "Cam chain selection missed tangent neighbors or included an end face");
        auto m = pair(MateKind::Cam, faces[seed], entity(K::Cylinder, {}, gp_Dir(0, 0, 1), 1));
        m.definition->references.clear();
        for (auto i : indices)
            m.definition->references.push_back({"cam", 0, faces[i]});
        m.definition->references.push_back(
            {"follower", 1, entity(K::Cylinder, {}, gp_Dir(0, 0, 1), 1)});
        std::vector<gp_Trsf> poses(2);
        prepareMate(m, poses);
        const auto cam = m.definition->references[0].geometry.path;
        require(std::abs(cam->length - (20 + 4 * pi)) < 1e-6, "Extruded cam path length is wrong");
        for (double at : {0.01, 0.25, 0.49, 0.75, 0.99})
        {
            const auto p = cam->point(at);
            const gp_Vec normal(m.definition->references[0].geometry.frame.Direction());
            const auto outward = gp_Vec(cam->tangent(at)).Crossed(normal).Normalized();
            const auto target = p.Translated(outward);
            require(gp_Vec(gp_Pnt(), p).Dot(outward) > 0,
                    "Cam roller offset points inside the cam");
            poses[1].SetRotation(gp_Quaternion(gp_Vec(0, 0, 1), normal));
            poses[1].SetTranslationPart(target.XYZ());
            const auto residual = mateResidual(m, poses, {at}, 10);
            for (std::size_t i = 0; i < residual.values.size(); ++i)
                require(std::abs(residual.values[i]) <= residual.tolerances[i] * 1.01,
                        "Cam contact failed across a tangent join: " + std::to_string(i) + "=" +
                            std::to_string(residual.values[i]));
        }
    }
    MateGeometry cone;
    for (TopExp_Explorer it(BRepPrimAPI_MakeCone(4, 2, 10).Shape(), TopAbs_FACE); it.More();
         it.Next())
    {
        auto g = mateGeometry(it.Current());
        if (g.kind == K::Cone)
            cone = g;
    }
    solve(bodies(), pair(MateKind::Tangent, cone, entity(K::Plane)), 4);

    // SolidWorks permits a straight edge tangent to a non-analytic surface.
    // Use a tangent segment at the middle of a torus face to verify both the
    // selection matrix and surface-contact equation without opening a UI.
    MateGeometry torusSurface;
    const auto torus = BRepPrimAPI_MakeTorus(8, 2).Shape();
    for (TopExp_Explorer it(torus, TopAbs_FACE); it.More(); it.Next())
    {
        torusSurface = mateGeometry(it.Current());
        if (torusSurface.kind == K::Surface)
            break;
    }
    const auto straightAxis = entity(K::Line);
    require(torusSurface.kind == K::Surface &&
                !compatibleMate(MateKind::Tangent, straightAxis, torusSurface),
            "An infinite axis should not be offered for finite-edge surface tangency");
    const double u = 0.5 * (torusSurface.u0 + torusSurface.u1);
    const double v = 0.5 * (torusSurface.v0 + torusSurface.v1);
    gp_Pnt contact;
    gp_Vec du, dv;
    torusSurface.surface->D1(u, v, contact, du, dv);
    du.Normalize();
    const auto tangentEdge = BRepBuilderAPI_MakeEdge(contact.Translated(du * -5),
                                                      contact.Translated(du * 5)).Edge();
    const auto tangentLine = mateGeometry(tangentEdge);
    require(compatibleMate(MateKind::Tangent, tangentLine, torusSurface),
            "A finite line and non-analytic surface should permit a tangent mate");
    auto lineSurface = pair(MateKind::Tangent, tangentLine, torusSurface);
    lineSurface.definition->motion.initialized = true;
    lineSurface.definition->motion.anchors = {gp_Trsf(), gp_Trsf()};
    lineSurface.definition->motion.coordinates = {
        (u - torusSurface.u0) / (torusSurface.u1 - torusSurface.u0),
        (v - torusSurface.v0) / (torusSurface.v1 - torusSurface.v0), 0.5};
    solve(bodies({}, 0), lineSurface, 5);
}
} // namespace
int main()
{
    try
    {
        standards();
        constrainedDragging();
        constrainedRotation();
        advanced();
        mechanical();
        relativeMechanicalMotion();
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
