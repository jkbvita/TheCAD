#include "modeling/PlanarEdgeTreatment.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <algorithm>
#include <cmath>
#include <optional>

namespace thecad::modeling {
namespace {
constexpr double epsilon = 1e-7;

gp_Pnt interpolate(const gp_Pnt& first, const gp_Pnt& last, double t)
{
    return gp_Pnt(first.XYZ() + (last.XYZ() - first.XYZ()) * t);
}

bool insideFace(const TopoDS_Face& face, const gp_Pnt& point)
{
    return BRepClass_FaceClassifier(face, point, epsilon).State() == TopAbs_IN;
}

bool samePlane(const TopoDS_Face& first, const TopoDS_Face& second)
{
    const auto a = BRepAdaptor_Surface(first).Plane();
    const auto b = BRepAdaptor_Surface(second).Plane();
    return a.Axis().Direction().IsParallel(b.Axis().Direction(), epsilon) &&
           a.Distance(b.Location()) <= epsilon;
}

std::optional<gp_Vec> faceInward(const TopoDS_Face& face, const gp_Vec& axis,
                                 const gp_Pnt& center, double sample)
{
    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) return {};
    const auto across = gp_Vec(surface.Plane().Axis().Direction()).Crossed(axis).Normalized();
    for (const double factor : {1.0, 0.1, 0.01}) {
        const bool positive = insideFace(face, center.Translated(across * (sample * factor)));
        const bool negative = insideFace(face, center.Translated(-across * (sample * factor)));
        if (positive != negative) return positive ? across : -across;
    }
    return {};
}

struct Cutter {
    TopoDS_Shape solid;
    TopoDS_Shape treatmentFace;
    TopoDS_Shape startFace, endFace;
};

std::optional<Cutter> buildCutter(const TopoDS_Shape& body, const TopoDS_Edge& edge,
                                  const TopoDS_Face& firstFace, const TopoDS_Face& secondFace,
                                  double size, bool chamfer)
{
    const BRepAdaptor_Curve curve(edge);
    if (curve.GetType() != GeomAbs_Line) return {};
    const auto first = curve.Value(curve.FirstParameter());
    const auto last = curve.Value(curve.LastParameter());
    const gp_Vec sweep(first, last);
    if (sweep.Magnitude() <= epsilon) return {};
    const auto center = interpolate(first, last, 0.5);
    const auto axis = sweep.Normalized();
    const auto sample = std::max(1e-4, std::min(size * 0.1, sweep.Magnitude() * 0.01));
    const auto a = faceInward(firstFace, axis, center, sample);
    const auto b = faceInward(secondFace, axis, center, sample);
    if (!a || !b) return {};
    const double halfAngle = a->Angle(*b) / 2;
    if (halfAngle < 1e-6 || std::abs(std::cos(halfAngle)) < 1e-6) return {};
    const auto bisector = (*a + *b).Normalized();
    const double setback = chamfer ? size : size / std::tan(halfAngle);
    if (setback < epsilon) return {};
    for (const double t : {0.25, 0.5, 0.75}) {
        const auto point = interpolate(first, last, t);
        if (!insideFace(firstFace, point.Translated(*a * (setback * 0.99))) ||
            !insideFace(secondFace, point.Translated(*b * (setback * 0.99)))) return {};
    }
    BRepClass3d_SolidClassifier classifier(body, center.Translated(bisector * std::min(sample, setback * 0.1)), epsilon);
    if (classifier.State() != TopAbs_IN) return {};

    const auto start = first.Translated(*a * setback);
    const auto finish = first.Translated(*b * setback);
    TopoDS_Edge profile;
    if (chamfer) {
        profile = BRepBuilderAPI_MakeEdge(start, finish).Edge();
    } else {
        const auto midpoint = first.Translated(bisector * (size / std::sin(halfAngle) - size));
        GC_MakeArcOfCircle arc(start, midpoint, finish);
        if (!arc.IsDone()) return {};
        profile = BRepBuilderAPI_MakeEdge(arc.Value()).Edge();
    }
    BRepBuilderAPI_MakeWire wire(BRepBuilderAPI_MakeEdge(first, start).Edge());
    wire.Add(profile);
    if (!wire.IsDone()) return {};
    const auto treatment = wire.Edge();
    wire.Add(BRepBuilderAPI_MakeEdge(finish, first).Edge());
    if (!wire.IsDone()) return {};
    BRepBuilderAPI_MakeFace face(wire.Wire(), true);
    if (!face.IsDone()) return {};
    BRepPrimAPI_MakePrism prism(face.Face(), sweep, true);
    if (!prism.IsDone() || prism.Generated(treatment).Extent() != 1) return {};
    return Cutter{prism.Shape(), prism.Generated(treatment).First(),
                  prism.FirstShape(), prism.LastShape()};
}
}

TopoDS_Shape cutPlanarEdgeTreatments(const TopoDS_Shape& body,
                                   const std::vector<TopoDS_Shape>& edges,
                                   double size, bool chamfer, TopologyNames& names,
                                   const std::string& featureId)
{
    if (body.IsNull() || edges.empty()) return {};
    // Only a continuation on the same two planes can share an endpoint.
    TopTools_IndexedMapOfShape vertices;
    std::vector<std::pair<TopoDS_Face, TopoDS_Face>> supportsAtVertex;
    TopTools_IndexedDataMapOfShapeListOfShape neighbors;
    TopExp::MapShapesAndUniqueAncestors(body, TopAbs_EDGE, TopAbs_FACE, neighbors);
    TopExp_Explorer solids(body, TopAbs_SOLID);
    if (!solids.More()) return {};
    solids.Next();
    if (solids.More()) return {};
    TopTools_ListOfShape arguments, tools;
    arguments.Append(body);
    auto resultNames = names;
    std::vector<std::string> treatmentKeys;
    for (const auto& selected : edges) {
        if (!neighbors.Contains(selected)) return {};
        const auto& faces = neighbors.FindFromKey(selected);
        if (faces.Extent() != 2) return {};
        const auto first = TopoDS::Face(faces.First());
        const auto second = TopoDS::Face(faces.Last());
        const auto cutter = buildCutter(body, TopoDS::Edge(selected), first, second, size, chamfer);
        const auto source = names.nameOf(selected);
        if (!cutter || source.empty()) return {};
        for (TopExp_Explorer points(selected, TopAbs_VERTEX); points.More(); points.Next()) {
            const int previous = vertices.FindIndex(points.Current());
            if (previous > 0) {
                const auto& [a, b] = supportsAtVertex[std::size_t(previous - 1)];
                if (!((samePlane(a, first) && samePlane(b, second)) ||
                      (samePlane(a, second) && samePlane(b, first)))) return {};
            } else {
                vertices.Add(points.Current());
                supportsAtVertex.emplace_back(first, second);
            }
        }
        tools.Append(cutter->solid);
        treatmentKeys.push_back(topologyKey("treatment", {featureId, source}));
        resultNames.add(treatmentKeys.back(), cutter->treatmentFace);
        resultNames.add(topologyKey("treatment-start", {featureId, source}), cutter->startFace);
        resultNames.add(topologyKey("treatment-end", {featureId, source}), cutter->endFace);
    }
    BRepAlgoAPI_Cut cut;
    cut.SetArguments(arguments);
    cut.SetTools(tools);
    cut.SetNonDestructive(true);
    cut.Build();
    if (!cut.IsDone() || cut.HasErrors()) return {};
    cut.SimplifyResult();
    const auto result = cut.Shape();
    TopExp_Explorer resultSolids(result, TopAbs_SOLID);
    if (!resultSolids.More() || !BRepCheck_Analyzer(result).IsValid()) return {};
    resultSolids.Next();
    if (resultSolids.More()) return {};
    resultNames = propagateTopology(resultNames, cut, result);
    for (const auto& key : treatmentKeys)
        if (resultNames.images.at(key).empty()) return {};
    resultNames.complete(result);
    names = std::move(resultNames);
    return result;
}
}
