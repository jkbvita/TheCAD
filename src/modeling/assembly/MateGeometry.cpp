#include "modeling/assembly/MateGeometry.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Line.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <optional>
namespace thecad::modeling::assembly
{
namespace
{
constexpr double pi = 3.14159265358979323846;
double curveLength(const Handle(Geom_Curve) & curve, double a, double b)
{
    GeomAdaptor_Curve c(curve, a, b);
    return GCPnts_AbscissaPoint::Length(c, a, b, 1e-8);
}
std::pair<std::size_t, double> parameter(const MatePath& path, double fraction)
{
    if (path.segments.empty() || path.length < 1e-9)
        throw std::runtime_error("Empty or degenerate mate path.");
    if (path.closed)
        fraction -= std::floor(fraction);
    else
        fraction = std::clamp(fraction, 0.0, 1.0);
    double remaining = fraction * path.length;
    for (std::size_t i = 0; i < path.segments.size(); ++i)
    {
        const auto& s = path.segments[i];
        if (remaining <= s.length || i + 1 == path.segments.size())
        {
            GeomAdaptor_Curve curve(s.curve, s.first, s.last);
            double distance = std::clamp(remaining, 0.0, s.length);
            if (s.reversed)
                distance = s.length - distance;
            if (distance < 1e-10)
                return {i, s.first};
            if (s.length - distance < 1e-10)
                return {i, s.last};
            GCPnts_AbscissaPoint point(curve, distance, s.first, 1e-8);
            if (!point.IsDone())
                throw std::runtime_error("Cannot evaluate distance along mate path.");
            return {i, point.Parameter()};
        }
        remaining -= s.length;
    }
    throw std::runtime_error("Invalid mate path parameter.");
}
void profile(MateGeometry& g, const TopoDS_Face& face)
{
    const auto wire = BRepTools::OuterWire(face);
    if (wire.IsNull())
        return;
    struct Edge
    {
        MateGeometry geometry;
        gp_Pnt first, last;
    };
    std::vector<Edge> edges;
    for (BRepTools_WireExplorer it(wire, face); it.More(); it.Next())
    {
        auto edge = mateGeometry(it.Current());
        if (edge.curve.IsNull())
            return;
        auto first = edge.curve->Value(edge.first), last = edge.curve->Value(edge.last);
        if (it.Current().Orientation() == TopAbs_REVERSED)
            std::swap(first, last);
        edges.push_back({edge, first, last});
    }
    if (edges.size() == 1 && edges[0].geometry.kind == MateGeometryKind::Circle)
    {
        const auto& circle = edges[0].geometry;
        const double span = std::abs(circle.last - circle.first);
        if (std::abs(span - 2 * pi) < 1e-6)
        {
            g.profile = MateProfileKind::Circle;
            g.profileFrame = circle.frame;
            g.profileWidth = g.profileHeight = 2 * circle.radius;
            g.profileSides = 0;
        }
        return;
    }
    std::vector<const Edge*> circles, lines;
    for (const auto& e : edges)
    {
        if (e.geometry.kind == MateGeometryKind::Circle)
            circles.push_back(&e);
        else if (e.geometry.kind == MateGeometryKind::Line)
            lines.push_back(&e);
        else
            return;
    }
    if (circles.size() == 2 && lines.size() == 2)
    {
        const auto& a = circles[0]->geometry;
        const auto& b = circles[1]->geometry;
        const double length = a.frame.Location().Distance(b.frame.Location());
        if (length > 1e-7 && std::abs(a.radius - b.radius) < 1e-6 &&
            std::abs(std::abs(a.last - a.first) - pi) < 1e-6 &&
            std::abs(std::abs(b.last - b.first) - pi) < 1e-6 &&
            std::abs(lines[0]->first.Distance(lines[0]->last) - length) < 1e-6 &&
            std::abs(lines[1]->first.Distance(lines[1]->last) - length) < 1e-6)
        {
            g.profile = MateProfileKind::Slot;
            g.profileFrame = gp_Ax3(a.frame.Location(), g.frame.Direction(),
                                    gp_Dir(gp_Vec(a.frame.Location(), b.frame.Location())));
            g.profileWidth = length;
            g.profileHeight = 2 * a.radius;
            MateGeometry centerline;
            centerline.kind = MateGeometryKind::Line;
            centerline.curve =
                new Geom_Line(gp_Lin(a.frame.Location(), g.profileFrame.XDirection()));
            centerline.first = 0;
            centerline.last = length;
            g.path = matePath({centerline});
            return;
        }
    }
    if (circles.size() == 4 && lines.empty())
    {
        for (std::size_t i = 0; i < circles.size(); ++i)
            for (std::size_t j = i + 1; j < circles.size(); ++j)
            {
                const auto& a = circles[i]->geometry;
                const auto& b = circles[j]->geometry;
                if (a.frame.Location().Distance(b.frame.Location()) > 1e-6 ||
                    std::abs(a.radius - b.radius) < 1e-6)
                    continue;
                const double radius = (a.radius + b.radius) * 0.5,
                             halfWidth = std::abs(a.radius - b.radius) * 0.5;
                bool caps = true;
                for (std::size_t k = 0; k < circles.size(); ++k)
                    if (k != i && k != j)
                        caps &= std::abs(circles[k]->geometry.radius - halfWidth) < 1e-6 &&
                                std::abs(circles[k]->geometry.frame.Location().Distance(
                                             a.frame.Location()) -
                                         radius) < 1e-6;
                if (!caps)
                    continue;
                g.profile = MateProfileKind::Slot;
                g.profileFrame = a.frame;
                g.profileHeight = 2 * halfWidth;
                MateGeometry arc;
                arc.kind = MateGeometryKind::Circle;
                arc.frame = a.frame;
                arc.radius = radius;
                arc.curve = new Geom_Circle(gp_Circ(a.frame.Ax2(), radius));
                arc.first = a.first;
                arc.last = a.last;
                g.path = matePath({arc});
                g.profileWidth = g.path->length;
                return;
            }
    }
    if (lines.size() == edges.size() && edges.size() >= 3)
    {
        gp_XYZ sum;
        for (const auto& e : edges)
            sum += e.first.XYZ();
        const gp_Pnt center(sum / double(edges.size()));
        const double radius = center.Distance(edges[0].first),
                     side = edges[0].first.Distance(edges[0].last);
        bool regular = radius > 1e-7;
        for (const auto& e : edges)
            regular &= std::abs(center.Distance(e.first) - radius) < 1e-6 &&
                       std::abs(e.first.Distance(e.last) - side) < 1e-6;
        if (regular)
        {
            g.profile =
                edges.size() == 4 ? MateProfileKind::Rectangle : MateProfileKind::RegularPolygon;
            g.profileFrame = gp_Ax3(center, g.frame.Direction(),
                                    gp_Dir(edges.size() == 4 ? gp_Vec(edges[0].first, edges[0].last)
                                                             : gp_Vec(center, edges[0].first)));
            g.profileWidth = g.profileHeight = edges.size() == 4 ? side : 2 * radius;
            g.profileSides = int(edges.size());
            return;
        }
    }
    // Recover a rectangle from its four actual supporting sides. Fillet/chamfer
    // corner edges may be present; extra parallel offsets reject L/U profiles.
    for (const auto* candidate : lines)
    {
        const gp_Dir x = candidate->geometry.frame.Direction(), y = g.frame.Direction().Crossed(x);
        std::vector<double> xs, ys;
        const auto insert = [](std::vector<double>& values, double v)
        {
            if (std::none_of(values.begin(), values.end(),
                             [&](double old) { return std::abs(old - v) < 1e-6; }))
                values.push_back(v);
        };
        for (const auto* e : lines)
        {
            const gp_Vec p(e->first.XYZ());
            if (e->geometry.frame.Direction().IsParallel(x, 1e-6))
                insert(ys, p.Dot(gp_Vec(y)));
            else if (e->geometry.frame.Direction().IsParallel(y, 1e-6))
                insert(xs, p.Dot(gp_Vec(x)));
        }
        if (xs.size() != 2 || ys.size() != 2)
            continue;
        const double dx = std::abs(xs[1] - xs[0]), dy = std::abs(ys[1] - ys[0]);
        if (dx < 1e-7 || dy < 1e-7)
            continue;
        const auto n = g.frame.Direction();
        const double z = gp_Vec(g.frame.Location().XYZ()).Dot(gp_Vec(n));
        const gp_Pnt center(gp_Vec(x).XYZ() * ((xs[0] + xs[1]) / 2) +
                            gp_Vec(y).XYZ() * ((ys[0] + ys[1]) / 2) + gp_Vec(n).XYZ() * z);
        bool inside = true;
        for (const auto& e : edges)
            for (const auto& p : {e.first, e.last})
                inside &= std::abs(gp_Vec(center, p).Dot(gp_Vec(x))) <= dx / 2 + 1e-6 &&
                          std::abs(gp_Vec(center, p).Dot(gp_Vec(y))) <= dy / 2 + 1e-6;
        if (inside)
        {
            g.profile = MateProfileKind::Rectangle;
            g.profileFrame = gp_Ax3(center, n, x);
            g.profileWidth = dx;
            g.profileHeight = dy;
            g.profileSides = 4;
            return;
        }
    }
}
} // namespace
gp_Pnt MatePath::point(double fraction) const
{
    const auto [i, t] = parameter(*this, fraction);
    return segments[i].curve->Value(t);
}
gp_Dir MatePath::tangent(double fraction) const
{
    const auto [i, t] = parameter(*this, fraction);
    gp_Pnt p;
    gp_Vec v;
    segments[i].curve->D1(t, p, v);
    if (segments[i].reversed)
        v.Reverse();
    if (v.SquareMagnitude() < 1e-20)
        throw std::runtime_error("Mate path has a singular tangent.");
    return gp_Dir(v);
}
std::shared_ptr<const MatePath> matePath(const std::vector<MateGeometry>& chain)
{
    auto path = std::make_shared<MatePath>();
    gp_Pnt start, last;
    for (const auto& e : chain)
    {
        if (e.curve.IsNull())
            throw std::runtime_error("Path selection requires contiguous curve edges.");
        MateCurveSegment s{e.curve, e.first, e.last, curveLength(e.curve, e.first, e.last), false};
        if (s.length < 1e-8)
            throw std::runtime_error("Degenerate path edge.");
        const auto a = e.curve->Value(e.first), b = e.curve->Value(e.last);
        if (path->segments.empty())
            start = a;
        else if (last.Distance(a) > 1e-6)
        {
            if (last.Distance(b) > 1e-6)
                throw std::runtime_error("Selected path edges are not contiguous in this order.");
            s.reversed = true;
        }
        last = s.reversed ? a : b;
        path->length += s.length;
        path->segments.push_back(s);
    }
    path->closed = !path->segments.empty() && last.Distance(start) < 1e-6;
    return path;
}
std::vector<std::size_t> camFaceChain(const std::vector<MateGeometry>& faces, std::size_t seed)
{
    if (seed >= faces.size())
        throw std::runtime_error("The selected cam face is missing.");
    const auto& selected = faces[seed];
    if (selected.kind != MateGeometryKind::Plane && selected.kind != MateGeometryKind::Cylinder)
        throw std::runtime_error("Select an extruded planar or cylindrical cam face.");
    const auto normal = [](const MateGeometry& g, const gp_Pnt& p)
    {
        if (g.kind == MateGeometryKind::Plane)
            return gp_Vec(g.frame.Direction());
        gp_Vec radial(g.frame.Location(), p);
        const gp_Vec axis(g.frame.Direction());
        radial -= axis * radial.Dot(axis);
        return radial.Normalized();
    };
    const auto tangentJoin = [&](const MateGeometry& a, const MateGeometry& b)
    {
        if (a.shape.IsNull() || b.shape.IsNull())
            return false;
        for (TopExp_Explorer first(a.shape, TopAbs_EDGE); first.More(); first.Next())
            for (TopExp_Explorer second(b.shape, TopAbs_EDGE); second.More(); second.Next())
                if (first.Current().IsSame(second.Current()))
                {
                    const BRepAdaptor_Curve curve(TopoDS::Edge(first.Current()));
                    const auto p =
                        curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2);
                    return std::abs(normal(a, p).Dot(normal(b, p))) > 1 - 1e-6;
                }
        return false;
    };
    for (const auto& axis : faces)
    {
        if (axis.kind != MateGeometryKind::Cylinder)
            continue;
        const auto direction = axis.frame.Direction();
        const auto eligible = [&](const MateGeometry& g)
        {
            return !g.shape.IsNull() && g.shape.ShapeType() == TopAbs_FACE &&
                   ((g.kind == MateGeometryKind::Cylinder &&
                     g.frame.Direction().IsParallel(direction, 1e-6)) ||
                    (g.kind == MateGeometryKind::Plane &&
                     std::abs(g.frame.Direction().Dot(direction)) < 1e-6));
        };
        if (!eligible(selected))
            continue;
        std::vector<std::size_t> chain{seed};
        for (std::size_t at = 0; at < chain.size(); ++at)
            for (std::size_t i = 0; i < faces.size(); ++i)
                if (eligible(faces[i]) && std::find(chain.begin(), chain.end(), i) == chain.end() &&
                    tangentJoin(faces[chain[at]], faces[i]))
                    chain.push_back(i);
        if (chain.size() > 128)
            throw std::runtime_error("The cam chain exceeds 128 faces.");
        std::vector<MateGeometry> geometry;
        for (auto i : chain)
            geometry.push_back(faces[i]);
        try
        {
            auto extrusion = direction;
            (void)camPath(geometry, extrusion);
            return chain;
        }
        catch (const std::exception&)
        { /* Try another extrusion direction, without selecting unrelated faces. */
        }
    }
    throw std::runtime_error("No closed tangent extruded cam chain contains the selected face. "
                             "Select its side faces explicitly.");
}
std::shared_ptr<const MatePath> camPath(const std::vector<MateGeometry>& faces, gp_Dir& direction)
{
    bool found = false;
    for (const auto& g : faces)
        if (g.kind == MateGeometryKind::Cylinder)
        {
            direction = g.frame.Direction();
            found = true;
            break;
        }
    if (!found)
        for (const auto& a : faces)
            for (const auto& b : faces)
                if (a.kind == MateGeometryKind::Plane && b.kind == MateGeometryKind::Plane)
                {
                    const auto cross =
                        gp_Vec(a.frame.Direction()).Crossed(gp_Vec(b.frame.Direction()));
                    if (cross.SquareMagnitude() > 1e-12)
                    {
                        direction = gp_Dir(cross);
                        found = true;
                        break;
                    }
                }
    if (!found)
        throw std::runtime_error("Select the closed tangent chain of extruded cam faces, including "
                                 "a cylindrical face or two nonparallel planes.");
    std::vector<MateGeometry> edges;
    double level = 0;
    bool levelSet = false;
    for (const auto& face : faces)
    {
        if ((face.kind != MateGeometryKind::Cylinder && face.kind != MateGeometryKind::Plane) ||
            face.shape.ShapeType() != TopAbs_FACE)
            throw std::runtime_error(
                "Cam selections must be cylindrical or planar extruded faces.");
        if (face.kind == MateGeometryKind::Cylinder &&
            !face.frame.Direction().IsParallel(direction, 1e-6))
            throw std::runtime_error("Cam faces must share one extrusion direction.");
        if (face.kind == MateGeometryKind::Plane &&
            std::abs(face.frame.Direction().Dot(direction)) > 1e-6)
            throw std::runtime_error(
                "A cam end face cannot be part of its follower surface chain.");
        std::optional<MateGeometry> best;
        double height = 1e100;
        for (TopExp_Explorer it(face.shape, TopAbs_EDGE); it.More(); it.Next())
        {
            const auto g = mateGeometry(it.Current());
            if (g.curve.IsNull())
                continue;
            const auto a = g.curve->Value(g.first), b = g.curve->Value(g.last),
                       mid = g.curve->Value((g.first + g.last) / 2);
            const double z = gp_Vec(a.XYZ()).Dot(gp_Vec(direction));
            if (std::abs(gp_Vec(a, b).Dot(gp_Vec(direction))) > 1e-6 ||
                std::abs(gp_Vec(a, mid).Dot(gp_Vec(direction))) > 1e-6)
                continue;
            if (z < height)
            {
                height = z;
                best = g;
            }
        }
        if (!best)
            throw std::runtime_error("Cannot extract the cam cross-section from a selected face.");
        if (!levelSet)
        {
            level = height;
            levelSet = true;
        }
        gp_Trsf shift;
        shift.SetTranslation(gp_Vec(direction) * (level - height));
        best->curve = Handle(Geom_Curve)::DownCast(best->curve->Transformed(shift));
        edges.push_back(*best);
    }
    std::vector<MateGeometry> ordered;
    ordered.push_back(edges.front());
    edges.erase(edges.begin());
    gp_Pnt end = ordered.back().curve->Value(ordered.back().last);
    while (!edges.empty())
    {
        const auto next = std::find_if(edges.begin(), edges.end(),
                                       [&](const auto& g)
                                       {
                                           return end.Distance(g.curve->Value(g.first)) < 1e-6 ||
                                                  end.Distance(g.curve->Value(g.last)) < 1e-6;
                                       });
        if (next == edges.end())
            throw std::runtime_error("Cam faces do not form one connected closed chain.");
        const bool reverse = end.Distance(next->curve->Value(next->first)) > 1e-6;
        end = next->curve->Value(reverse ? next->first : next->last);
        ordered.push_back(*next);
        edges.erase(next);
    }
    const auto path = matePath(ordered);
    if (!path->closed)
        throw std::runtime_error("Cam face chain is not closed.");
    for (std::size_t i = 0; i < path->segments.size(); ++i)
    {
        const auto& a = path->segments[i];
        const auto& b = path->segments[(i + 1) % path->segments.size()];
        gp_Pnt p;
        gp_Vec ta, tb;
        a.curve->D1(a.reversed ? a.first : a.last, p, ta);
        b.curve->D1(b.reversed ? b.last : b.first, p, tb);
        if (a.reversed)
            ta.Reverse();
        if (b.reversed)
            tb.Reverse();
        if (gp_Dir(ta).Dot(gp_Dir(tb)) < 1 - 1e-6)
            throw std::runtime_error("Cam faces must be tangent along the entire closed chain.");
    }
    // Surface-edge traversal can start on either side of the same face. Normalize
    // winding so the external roller offset is independent of the picked seed.
    double area = 0;
    const auto origin = path->point(0);
    for (int i = 0; i < 128; ++i)
        area += gp_Vec(origin, path->point(i / 128.0))
                    .Crossed(gp_Vec(origin, path->point((i + 1) / 128.0)))
                    .Dot(gp_Vec(direction));
    if (std::abs(area) < 1e-12)
        throw std::runtime_error("The cam cross-section has no unambiguous interior.");
    if (area < 0)
    {
        auto oriented = std::make_shared<MatePath>(*path);
        std::reverse(oriented->segments.begin(), oriented->segments.end());
        for (auto& segment : oriented->segments)
            segment.reversed = !segment.reversed;
        return oriented;
    }
    return path;
}
MateGeometry mateGeometry(const DatumGeometry& d, bool frame)
{
    MateGeometry g;
    g.kind = frame                        ? MateGeometryKind::Frame
             : d.kind == DatumKind::Point ? MateGeometryKind::Point
             : d.kind == DatumKind::Plane ? MateGeometryKind::Plane
             : d.cylinderRadius > 0       ? MateGeometryKind::Cylinder
                                          : MateGeometryKind::Line;
    g.frame = d.frame;
    g.radius = d.cylinderRadius;
    return g;
}
MateGeometry mateGeometry(const TopoDS_Shape& shape)
{
    MateGeometry g;
    g.shape = shape;
    if (shape.IsNull())
        throw std::runtime_error("Missing mate geometry.");
    if (shape.ShapeType() == TopAbs_VERTEX)
    {
        g.frame.SetLocation(BRep_Tool::Pnt(TopoDS::Vertex(shape)));
        return g;
    }
    if (shape.ShapeType() == TopAbs_EDGE)
    {
        const auto edge = TopoDS::Edge(shape);
        BRepAdaptor_Curve c(edge);
        g.first = c.FirstParameter();
        g.last = c.LastParameter();
        TopLoc_Location location;
        g.curve = BRep_Tool::Curve(edge, location, g.first, g.last);
        if (g.curve.IsNull())
            throw std::runtime_error("Mate edge has no 3D curve.");
        if (!location.IsIdentity())
            g.curve = Handle(Geom_Curve)::DownCast(g.curve->Transformed(location.Transformation()));
        if (c.GetType() == GeomAbs_Line)
        {
            g.kind = MateGeometryKind::Line;
            g.frame = gp_Ax3(c.Line().Location(), c.Line().Direction());
        }
        else if (c.GetType() == GeomAbs_Circle)
        {
            g.kind = MateGeometryKind::Circle;
            g.frame = gp_Ax3(c.Circle().Position());
            g.radius = c.Circle().Radius();
            if (std::abs(g.last - g.first - 2 * pi) < 1e-6)
            {
                g.profile = MateProfileKind::Circle;
                g.profileFrame = g.frame;
                g.profileWidth = g.profileHeight = 2 * g.radius;
            }
        }
        else
            g.kind = MateGeometryKind::Curve;
        return g;
    }
    if (shape.ShapeType() != TopAbs_FACE)
        throw std::runtime_error("Select a vertex, edge, face or datum for mating.");
    const auto face = TopoDS::Face(shape);
    BRepAdaptor_Surface s(face);
    g.reversed = face.Orientation() == TopAbs_REVERSED;
    TopLoc_Location location;
    g.surface = BRep_Tool::Surface(face, location);
    if (!location.IsIdentity())
        g.surface =
            Handle(Geom_Surface)::DownCast(g.surface->Transformed(location.Transformation()));
    BRepTools::UVBounds(face, g.u0, g.u1, g.v0, g.v1);
    switch (s.GetType())
    {
    case GeomAbs_Plane:
        g.kind = MateGeometryKind::Plane;
        g.frame = planarFaceAxes(face);
        profile(g, face);
        break;
    case GeomAbs_Cylinder:
        g.kind = MateGeometryKind::Cylinder;
        g.frame = s.Cylinder().Position();
        g.radius = s.Cylinder().Radius();
        break;
    case GeomAbs_Cone:
        g.kind = MateGeometryKind::Cone;
        g.frame = s.Cone().Position();
        g.radius = s.Cone().RefRadius();
        g.semiAngle = s.Cone().SemiAngle();
        break;
    case GeomAbs_Sphere:
        g.kind = MateGeometryKind::Sphere;
        g.frame = s.Sphere().Position();
        g.radius = s.Sphere().Radius();
        break;
    default:
        g.kind = MateGeometryKind::Surface;
        break;
    }
    return g;
}
bool mateGeometryDirection(const MateGeometry& g)
{
    return g.kind == MateGeometryKind::Line || g.kind == MateGeometryKind::Plane ||
           g.kind == MateGeometryKind::Circle || g.kind == MateGeometryKind::Cylinder ||
           g.kind == MateGeometryKind::Cone || g.kind == MateGeometryKind::Frame;
}
const char* mateGeometryName(MateGeometryKind k)
{
    const char* names[] = {"Point", "Line / Axis", "Plane", "Circle / Arc", "Cylinder",
                           "Cone",  "Sphere",      "Curve", "Surface",      "Coordinate frame"};
    return names[int(k)];
}
} // namespace thecad::modeling::assembly
