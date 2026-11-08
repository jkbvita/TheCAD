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
#include <gp_Lin.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

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
    TopoDS_Shape firstSide, secondSide;
    std::vector<TopoDS_Vertex> creaseVertices;
};

std::optional<Cutter> buildCutter(const TopoDS_Shape& body, const TopoDS_Edge& edge,
                                  const TopoDS_Face& firstFace, const TopoDS_Face& secondFace,
                                  double size, bool chamfer, bool legacyEnds,
                                  const TopTools_IndexedDataMapOfShapeListOfShape& neighbors,
                                  const TopTools_IndexedDataMapOfShapeListOfShape& incident)
{
    const BRepAdaptor_Curve curve(edge);
    if (curve.GetType() != GeomAbs_Line) return {};
    auto first = curve.Value(curve.FirstParameter());
    auto last = curve.Value(curve.LastParameter());
    TopoDS_Vertex firstVertex, lastVertex;
    TopExp::Vertices(TopoDS::Edge(edge.Oriented(TopAbs_FORWARD)), firstVertex, lastVertex);
    if (firstVertex.IsNull() || lastVertex.IsNull()) return {};
    if (!legacyEnds) {
        // Opposite picks on a split crease must sweep the same oriented section.
        const auto direction = last.XYZ() - first.XYZ();
        const std::array components{direction.X(), direction.Y(), direction.Z()};
        const auto major = std::max_element(components.begin(), components.end(),
            [](double a, double b) { return std::abs(a) < std::abs(b); });
        if (*major > 0) {
            std::swap(first, last);
            std::swap(firstVertex, lastVertex);
        }
    }
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

    auto toolStart = first;
    auto toolEnd = last;
    std::vector<TopoDS_Vertex> creaseVertices{firstVertex, lastVertex};
    if (!legacyEnds) {
        // Continue through topological splits of the same straight crease.
        const auto extendThroughSplits = [&](gp_Pnt tip, TopoDS_Vertex vertex, const gp_Vec& outward) {
            const gp_Lin path(tip, gp_Dir(outward));
            while (!vertex.IsNull() && incident.Contains(vertex)) {
                TopoDS_Vertex nextVertex;
                gp_Pnt nextTip;
                for (const auto& continuation : incident.FindFromKey(vertex)) {
                    const auto& faces = neighbors.FindFromKey(continuation);
                    if (faces.Extent() != 2) continue;
                    const auto faceA = TopoDS::Face(faces.First());
                    const auto faceB = TopoDS::Face(faces.Last());
                    if (BRepAdaptor_Surface(faceA).GetType() != GeomAbs_Plane ||
                        BRepAdaptor_Surface(faceB).GetType() != GeomAbs_Plane ||
                        !((samePlane(firstFace, faceA) && samePlane(secondFace, faceB)) ||
                          (samePlane(firstFace, faceB) && samePlane(secondFace, faceA)))) continue;
                    const auto candidate = TopoDS::Edge(continuation);
                    if (BRepAdaptor_Curve(candidate).GetType() != GeomAbs_Line) continue;
                    TopoDS_Vertex v1, v2;
                    TopExp::Vertices(candidate, v1, v2);
                    const auto other = v1.IsSame(vertex) ? v2 : v1;
                    if (other.IsNull()) continue;
                    const auto point = BRep_Tool::Pnt(other);
                    if (path.Distance(point) > epsilon ||
                        gp_Vec(tip, point).Dot(outward) <= epsilon) continue;
                    if (!nextVertex.IsNull()) return tip;
                    nextVertex = other;
                    const auto along = gp_Vec(path.Location(), point).Dot(outward);
                    nextTip = path.Location().Translated(outward * along);
                }
                if (nextVertex.IsNull()) break;
                vertex = nextVertex;
                tip = nextTip;
                creaseVertices.push_back(vertex);
            }
            return tip;
        };
        // Give the actual surrounding faces room to trim the exact-radius section.
        // A tool whose artificial end still bounds the result will be rejected.
        const double overrun = 3 * setback + 100 * epsilon;
        toolStart = extendThroughSplits(first, firstVertex, -axis).Translated(-axis * overrun);
        toolEnd = extendThroughSplits(last, lastVertex, axis).Translated(axis * overrun);
    }
    const auto start = toolStart.Translated(*a * setback);
    const auto finish = toolStart.Translated(*b * setback);
    TopoDS_Edge profile;
    if (chamfer) {
        profile = BRepBuilderAPI_MakeEdge(start, finish).Edge();
    } else {
        const auto midpoint = toolStart.Translated(bisector * (size / std::sin(halfAngle) - size));
        GC_MakeArcOfCircle arc(start, midpoint, finish);
        if (!arc.IsDone()) return {};
        profile = BRepBuilderAPI_MakeEdge(arc.Value()).Edge();
    }
    BRepBuilderAPI_MakeWire wire(BRepBuilderAPI_MakeEdge(toolStart, start).Edge());
    const auto firstSide = wire.Edge();
    wire.Add(profile);
    if (!wire.IsDone()) return {};
    const auto treatment = wire.Edge();
    wire.Add(BRepBuilderAPI_MakeEdge(finish, toolStart).Edge());
    if (!wire.IsDone()) return {};
    const auto secondSide = wire.Edge();
    BRepBuilderAPI_MakeFace face(wire.Wire(), true);
    if (!face.IsDone()) return {};
    BRepPrimAPI_MakePrism prism(face.Face(), gp_Vec(toolStart, toolEnd), true);
    if (!prism.IsDone() || prism.Generated(treatment).Extent() != 1) return {};
    Cutter cutter{prism.Shape(), prism.Generated(treatment).First(),
                  prism.FirstShape(), prism.LastShape(), {}, {}, std::move(creaseVertices)};
    if (!legacyEnds) {
        const auto& firstFaces = prism.Generated(firstSide);
        if (firstFaces.Extent() != 1) return {};
        cutter.firstSide = firstFaces.First();
        const auto& secondFaces = prism.Generated(secondSide);
        if (secondFaces.Extent() != 1) return {};
        cutter.secondSide = secondFaces.First();
    }
    return cutter;
}

TopoDS_Vertex draftJunction(const TopoDS_Shape& first, const TopoDS_Shape& second,
    const TopTools_IndexedDataMapOfShapeListOfShape& neighbors,
    const TopTools_IndexedDataMapOfShapeListOfShape& incident)
{
    const auto a = TopoDS::Edge(first);
    const auto b = TopoDS::Edge(second);
    TopoDS_Vertex shared;
    if (!TopExp::CommonVertex(a, b, shared) || incident.FindFromKey(shared).Extent() != 4 ||
        BRepAdaptor_Curve(a).GetType() != GeomAbs_Line ||
        BRepAdaptor_Curve(b).GetType() != GeomAbs_Line) return {};
    const auto awayFrom = [&](const TopoDS_Edge& edge) {
        TopoDS_Vertex start, end;
        TopExp::Vertices(edge, start, end);
        const auto& other = start.IsSame(shared) ? end : start;
        return gp_Vec(BRep_Tool::Pnt(shared), BRep_Tool::Pnt(other));
    };
    const auto da = awayFrom(a);
    const auto db = awayFrom(b);
    if (da.Dot(db) >= 0 || da.IsParallel(db, epsilon)) return {};

    const auto& supportsA = neighbors.FindFromKey(a);
    const auto& supportsB = neighbors.FindFromKey(b);
    if (supportsA.Extent() != 2 || supportsB.Extent() != 2) return {};
    for (const auto& support : supportsA)
        if (supportsB.Contains(support)) return {};
    // Each side of the shoulder must join one support from each fillet run.
    for (const auto& edge : incident.FindFromKey(shared)) {
        if (edge.IsSame(a) || edge.IsSame(b)) continue;
        const auto& faces = neighbors.FindFromKey(edge);
        if (faces.Extent() != 2 ||
            !((supportsA.Contains(faces.First()) && supportsB.Contains(faces.Last())) ||
              (supportsB.Contains(faces.First()) && supportsA.Contains(faces.Last())))) return {};
    }
    return shared;
}

bool hasSharpJoin(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    TopTools_IndexedMapOfShape firstEdges;
    TopExp::MapShapes(a, TopAbs_EDGE, firstEdges);
    TopTools_ListOfShape join;
    for (TopExp_Explorer edges(b, TopAbs_EDGE); edges.More(); edges.Next())
        if (firstEdges.Contains(edges.Current()) &&
            BRep_Tool::Continuity(TopoDS::Edge(edges.Current()), TopoDS::Face(a), TopoDS::Face(b)) ==
                GeomAbs_C0) join.Append(edges.Current());
    if (join.IsEmpty()) return false;
    BRepBuilderAPI_MakeWire seam;
    seam.Add(join);
    return seam.IsDone() && !seam.Wire().Closed();
}
}

TopoDS_Shape cutPlanarEdgeTreatments(const TopoDS_Shape& body,
                                   const std::vector<TopoDS_Shape>& edges,
                                   double size, bool chamfer, TopologyNames& names,
                                   const std::string& featureId, EdgeTermination termination)
{
    if (body.IsNull() || edges.empty()) return {};
    const bool legacyEnds = termination == EdgeTermination::Legacy;
    const bool tryMiter = termination == EdgeTermination::PreserveDraftJunction;
    if (tryMiter && (chamfer || edges.size() != 2)) return {};
    // Separate creases may meet only at a validated draft junction.
    TopTools_IndexedMapOfShape vertices;
    std::vector<std::pair<TopoDS_Face, TopoDS_Face>> supportsAtVertex;
    TopTools_IndexedDataMapOfShapeListOfShape neighbors;
    TopExp::MapShapesAndUniqueAncestors(body, TopAbs_EDGE, TopAbs_FACE, neighbors);
    TopTools_IndexedDataMapOfShapeListOfShape incident;
    if (!legacyEnds) TopExp::MapShapesAndUniqueAncestors(body, TopAbs_VERTEX, TopAbs_EDGE, incident);
    TopoDS_Vertex junction;
    if (tryMiter) {
        junction = draftJunction(edges[0], edges[1], neighbors, incident);
        if (junction.IsNull()) return {};
    }
    TopExp_Explorer solids(body, TopAbs_SOLID);
    if (!solids.More()) return {};
    solids.Next();
    if (solids.More()) return {};
    TopTools_ListOfShape arguments, tools;
    arguments.Append(body);
    auto resultNames = names;
    std::vector<std::string> treatmentKeys;
    std::vector<TopoDS_Shape> artificialFaces;
    for (const auto& selected : edges) {
        if (!neighbors.Contains(selected)) return {};
        const auto& faces = neighbors.FindFromKey(selected);
        if (faces.Extent() != 2) return {};
        const auto first = TopoDS::Face(faces.First());
        const auto second = TopoDS::Face(faces.Last());
        const auto cutter = buildCutter(body, TopoDS::Edge(selected), first, second, size, chamfer,
                                        legacyEnds, neighbors, incident);
        const auto source = names.nameOf(selected);
        if (!cutter || source.empty()) return {};
        for (const auto& point : cutter->creaseVertices) {
            const int previous = vertices.FindIndex(point);
            if (previous > 0) {
                const auto& [a, b] = supportsAtVertex[std::size_t(previous - 1)];
                if (!point.IsSame(junction) &&
                    !((samePlane(a, first) && samePlane(b, second)) ||
                      (samePlane(a, second) && samePlane(b, first)))) return {};
            } else {
                vertices.Add(point);
                supportsAtVertex.emplace_back(first, second);
            }
        }
        tools.Append(cutter->solid);
        treatmentKeys.push_back(topologyKey("treatment", {featureId, source}));
        resultNames.add(treatmentKeys.back(), cutter->treatmentFace);
        if (legacyEnds) {
            resultNames.add(topologyKey("treatment-start", {featureId, source}), cutter->startFace);
            resultNames.add(topologyKey("treatment-end", {featureId, source}), cutter->endFace);
        } else {
            artificialFaces.insert(artificialFaces.end(), {cutter->startFace, cutter->endFace,
                                                            cutter->firstSide, cutter->secondSide});
        }
    }
    BRepAlgoAPI_Cut cut;
    cut.SetArguments(arguments);
    cut.SetTools(tools);
    cut.SetNonDestructive(true);
    cut.Build();
    if (!cut.IsDone() || cut.HasErrors()) return {};
    if (!legacyEnds) {
        // A fragment that also comes from a stock face is an existing boundary,
        // even when the Boolean reports it in the cutter's history too.
        TopTools_IndexedMapOfShape resultFaces;
        TopExp::MapShapes(cut.Shape(), TopAbs_FACE, resultFaces);
        TopTools_IndexedMapOfShape stockFaces;
        for (TopExp_Explorer faces(body, TopAbs_FACE); faces.More(); faces.Next()) {
            const auto& face = faces.Current();
            const auto& images = cut.Modified(face);
            stockFaces.Add(face);
            for (const auto& image : images)
                stockFaces.Add(image);
        }
        for (const auto& face : artificialFaces) {
            if (resultFaces.Contains(face) && !stockFaces.Contains(face)) return {};
            for (const auto& fragment : cut.Modified(face))
                if (resultFaces.Contains(fragment) && !stockFaces.Contains(fragment)) return {};
        }
    }
    cut.SimplifyResult();
    const auto result = cut.Shape();
    TopExp_Explorer resultSolids(result, TopAbs_SOLID);
    if (!resultSolids.More() || !BRepCheck_Analyzer(result).IsValid()) return {};
    resultSolids.Next();
    if (resultSolids.More()) return {};
    resultNames = propagateTopology(resultNames, cut, result);
    for (const auto& key : treatmentKeys)
        if (resultNames.images.at(key).empty()) return {};
    if (tryMiter) {
        const auto& first = resultNames.images.at(treatmentKeys[0]);
        const auto& second = resultNames.images.at(treatmentKeys[1]);
        if (first.size() != 1 || second.size() != 1 || !hasSharpJoin(first[0], second[0])) return {};
    }
    resultNames.complete(result);
    names = std::move(resultNames);
    return result;
}
}
