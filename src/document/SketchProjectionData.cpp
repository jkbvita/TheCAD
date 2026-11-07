#include "document/SketchProjectionData.h"
#include "document/PartDocument.h"
#include "modeling/ConstraintSolver.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace thecad::document
{
namespace
{
using namespace modeling;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

SketchEntity projectEdge(const Sketch& sketch, const TopoDS_Edge& edge,
                         const TopoDS_Vertex& start = {}, bool coplanar = false)
{
    const auto frame = sketchAxes(sketch);
    const auto local = [&](const gp_Pnt& p) {
        const gp_Vec delta(frame.Location(), p);
        require(!coplanar || std::abs(delta.Dot(gp_Vec(frame.Direction()))) < 1e-6,
                "The edge is outside the sketch plane.");
        return Point2{delta.Dot(gp_Vec(frame.XDirection())), delta.Dot(gp_Vec(frame.YDirection()))};
    };
    BRepAdaptor_Curve curve(edge);
    if (curve.GetType() == GeomAbs_Line)
    {
        auto first = curve.Value(curve.FirstParameter());
        auto last = curve.Value(curve.LastParameter());
        if (!start.IsNull())
        {
            TopoDS_Vertex firstVertex, lastVertex;
            TopExp::Vertices(edge, firstVertex, lastVertex);
            require(start.IsSame(firstVertex) || start.IsSame(lastVertex),
                    "The converted edge lost its named endpoint.");
            if (BRep_Tool::Pnt(start).Distance(last) < BRep_Tool::Pnt(start).Distance(first))
                std::swap(first, last);
        }
        const auto a = local(first), b = local(last);
        require(std::hypot(a.x - b.x, a.y - b.y) > 1e-6,
                "This edge projects to a point on the sketch plane.");
        return {EntityKind::Line, a, b};
    }
    if (curve.GetType() == GeomAbs_Circle &&
        std::abs(curve.LastParameter() - curve.FirstParameter() - 6.28318530717958647692) < 1e-7)
    {
        const auto circle = curve.Circle();
        require(circle.Axis().Direction().IsParallel(frame.Direction(), 1e-8),
                "An angled circle projects to an ellipse. Select a parallel circle.");
        return {EntityKind::Circle, local(circle.Location()), {}, circle.Radius()};
    }
    throw std::runtime_error("Convert Entities supports straight edges and complete circles.");
}

SketchProjection edgeOption(const Sketch& sketch, const TopoDS_Edge& edge,
                            const std::string& body, const TopologyNames& names, bool coplanar)
{
    SketchProjection option;
    option.link.sourceFeature = body;
    option.link.edgeName = names.nameOf(edge);
    require(!option.link.edgeName.empty(), "This model edge has no unique persistent name.");
    TopoDS_Vertex first, last;
    TopExp::Vertices(edge, first, last);
    if (!first.IsNull() && !first.IsSame(last))
        option.link.startVertex = names.nameOf(first);
    if (option.link.startVertex.empty())
        first.Nullify();
    option.geometry = projectEdge(sketch, edge, first, coplanar);
    return option;
}

void addFaceEdges(TopTools_IndexedMapOfShape& edges, const TopoDS_Face& face, bool innerLoops)
{
    require(BRepAdaptor_Surface(face).GetType() == GeomAbs_Plane,
            "Select a planar face or supported model edges.");
    if (innerLoops)
        TopExp::MapShapes(face, TopAbs_EDGE, edges);
    else
        TopExp::MapShapes(BRepTools::OuterWire(face), TopAbs_EDGE, edges);
}
}

void detail::refreshSketchProjections(
    modeling::Sketch& sketch, const std::map<std::string, modeling::TopologyNames>& namedBodies)
{
    validateSketchModifiers(sketch);
    for (const auto& link : sketch.projections)
    {
        const auto body = namedBodies.find(link.sourceFeature);
        require(body != namedBodies.end(), "Converted geometry requires an earlier solid.");
        const auto edge = TopoDS::Edge(body->second.resolve(link.edgeName, TopAbs_EDGE));
        TopoDS_Vertex vertex;
        if (!link.startVertex.empty())
            vertex = TopoDS::Vertex(body->second.resolve(link.startVertex, TopAbs_VERTEX));
        auto projected = projectEdge(sketch, edge, vertex);
        const auto old = std::find_if(sketch.entities.begin(), sketch.entities.end(),
                                      [&](const auto& item) { return item.id == link.entityId; });
        require(old->kind == projected.kind, "The source edge changed curve type.");
        projected.id = old->id;
        projected.construction = old->construction;
        *old = projected;
    }
}

std::vector<modeling::SketchProjection> PartDocument::sketchBoundaryReferences(
    const modeling::Sketch& sketch) const
{
    if (!sketch.attachment)
        return {};
    const auto& attachment = *sketch.attachment;
    const auto body = m_namedBodies.find(attachment.sourceFeature);
    if (body == m_namedBodies.end() || attachment.topologyName.empty())
        return {};
    TopTools_IndexedMapOfShape edges;
    const auto face = TopoDS::Face(body->second.resolve(attachment.topologyName, TopAbs_FACE));
    addFaceEdges(edges, face, true);
    std::vector<SketchProjection> result;
    for (int i = 1; i <= edges.Extent(); ++i)
        try
        {
            result.push_back(edgeOption(sketch, TopoDS::Edge(edges(i)), body->first, body->second, true));
        }
        catch (const std::exception&)
        {
            // Unsupported edges stay visible as solid geometry.
        }
    return result;
}

modeling::Sketch PartDocument::convertSketchGeometry(
    const std::string& sketchId, const modeling::Sketch& sketch,
    const std::vector<modeling::SelectionRef>& selection, bool innerLoops, bool construction) const
{
    std::set<std::string> earlier;
    for (const auto& feature : m_features)
    {
        if (feature.id == sketchId)
            break;
        if (isSolidFeature(feature.kind))
            earlier.insert(feature.id);
    }
    require(std::any_of(m_features.begin(), m_features.end(),
                         [&](const auto& f) { return f.id == sketchId && f.kind == FeatureKind::Sketch; }),
            "The edited sketch is unavailable.");
    auto result = sketch;
    const auto convert = [&](const std::string& source, const TopTools_IndexedMapOfShape& edges) {
        require(earlier.count(source) != 0, "Select model geometry created before this sketch.");
        for (int i = 1; i <= edges.Extent(); ++i)
            useSketchProjection(result, edgeOption(sketch, TopoDS::Edge(edges(i)), source,
                                                    m_namedBodies.at(source), false), construction);
    };
    if (selection.empty())
    {
        require(sketch.attachment && !sketch.attachment->topologyName.empty(),
                "Select a planar model face or model edges in the viewport.");
        const auto& support = *sketch.attachment;
        require(earlier.count(support.sourceFeature) != 0, "The sketch support is unavailable.");
        const auto face = m_namedBodies.at(support.sourceFeature).resolve(support.topologyName, TopAbs_FACE);
        TopTools_IndexedMapOfShape edges;
        addFaceEdges(edges, TopoDS::Face(face), innerLoops);
        convert(support.sourceFeature, edges);
    }
    for (const auto& item : selection)
    {
        require(item.kind == SelectionKind::Face || item.kind == SelectionKind::Edge,
                "Select planar faces or model edges to convert.");
        require(earlier.count(item.featureId) != 0, "Select model geometry created before this sketch.");
        const auto type = item.kind == SelectionKind::Face ? TopAbs_FACE : TopAbs_EDGE;
        TopTools_IndexedMapOfShape shapes, edges;
        TopExp::MapShapes(m_bodies.at(item.featureId), type, shapes);
        require(item.index > 0 && item.index <= shapes.Extent(), "The selected face or edge is unavailable.");
        if (type == TopAbs_FACE)
            addFaceEdges(edges, TopoDS::Face(shapes(item.index)), innerLoops);
        else
            edges.Add(shapes(item.index));
        convert(item.featureId, edges);
    }
    const bool changed = result.entities.size() != sketch.entities.size() ||
        !std::equal(result.entities.begin(), result.entities.end(), sketch.entities.begin(),
                    [](const auto& a, const auto& b) { return a.construction == b.construction; });
    require(changed, "The selected outline is already in the sketch.");
    return requireSolvedSketch(solvePlanarSketch(result));
}
}
