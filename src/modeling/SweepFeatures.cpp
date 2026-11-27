#include "modeling/SolidFeatures.h"
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace thecad::modeling
{
namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void requireSolid(const TopoDS_Shape& shape)
{
    require(!shape.IsNull() && TopExp_Explorer(shape, TopAbs_SOLID).More() &&
                BRepCheck_Analyzer(shape).IsValid(),
            "The sweep must leave valid solid bodies. Check the profile size and path.");
}

double volume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}

TopoDS_Shape combine(BRepAlgoAPI_BooleanOperation& operation, const TopoDS_Shape& base,
                     const TopTools_ListOfShape& tools, TopologyNames& names)
{
    TopTools_ListOfShape arguments;
    arguments.Append(base);
    operation.SetArguments(arguments);
    operation.SetTools(tools);
    operation.SetNonDestructive(true);
    operation.Build();
    require(operation.IsDone() && !operation.HasErrors(),
            "Cannot combine the swept material with the solid.");
    operation.SimplifyResult();
    const auto result = operation.Shape();
    requireSolid(result);
    names = propagateTopology(names, operation, result);
    return result;
}

TopoDS_Shape sweepWire(const TopoDS_Wire& spine, const TopoDS_Wire& section,
                       const TopologyNames& boundaries, TopologyNames& names,
                       const std::string& key, bool outer)
{
    TopoDS_Vertex start, end;
    TopExp::Vertices(spine, start, end);
    BRepOffsetAPI_MakePipeShell sweep(spine);
    sweep.SetTransitionMode(BRepBuilderAPI_RightCorner);
    sweep.Add(TopoDS::Wire(section.Oriented(TopAbs_FORWARD)), start);
    sweep.Build();
    require(sweep.IsDone() && sweep.MakeSolid(),
            "Cannot sweep this profile along the path. Check the corners and profile size.");
    const auto result = sweep.Shape();
    requireSolid(result);

    if (outer)
    {
        const auto first = sweep.FirstShape();
        const auto last = sweep.LastShape();
        if (!first.IsNull() && first.ShapeType() == TopAbs_FACE)
            names.add(topologyKey("sweep-start", {key}), first);
        if (!last.IsNull() && last.ShapeType() == TopAbs_FACE)
            names.add(topologyKey("sweep-end", {key}), last);
    }
    TopTools_IndexedMapOfShape sectionEdges;
    TopExp::MapShapes(section, TopAbs_EDGE, sectionEdges);
    for (const auto& [boundary, edges] : boundaries.images)
        for (const auto& edge : edges)
            if (sectionEdges.Contains(edge))
                for (const auto& image : sweep.Generated(edge))
                    if (image.ShapeType() == TopAbs_FACE)
                        names.add(topologyKey("sweep-side", {key, boundary}), image);
    return result;
}
} // namespace

TopoDS_Shape makeSweep(const TopoDS_Shape& base, const Sketch& profile, const Sketch& path,
                       const std::vector<std::string>& region, bool cut,
                       TopologyNames& names, const std::string& featureKey)
{
    require(!cut || !base.IsNull(), "Swept Cut requires an existing solid.");
    const std::set<std::string> selected(region.begin(), region.end());
    require(!selected.empty() && selected.size() == region.size(), "Select one closed profile region.");
    const auto contours = closedContours(profile);
    const auto contour = std::find_if(contours.begin(), contours.end(), [&](const auto& candidate) {
        return std::set<std::string>(candidate.entityIds.begin(), candidate.entityIds.end()) == selected;
    });
    require(contour != contours.end(), "The sweep profile region changed or disappeared. Reselect it.");

    std::vector<gp_Pnt> points;
    for (const auto point : openSketchPath(path))
        points.push_back(sketchPoint(path, point));
    const auto plane = sketchAxes(profile);
    const gp_Vec normal(plane.Direction());
    const auto onPlane = [&](const gp_Pnt& point) {
        return std::abs(gp_Vec(plane.Location(), point).Dot(normal)) <= 1e-6;
    };
    const bool startOnPlane = onPlane(points.front()), endOnPlane = onPlane(points.back());
    require(startOnPlane != endOnPlane,
            "Exactly one endpoint of the sweep path must lie on the profile plane.");
    if (endOnPlane)
        std::reverse(points.begin(), points.end());
    require(!onPlane(points[1]), "The first sweep segment must leave the profile plane.");
    BRepBuilderAPI_MakeWire spine;
    for (std::size_t i = 1; i < points.size(); ++i)
        spine.Add(BRepBuilderAPI_MakeEdge(points[i - 1], points[i]).Edge());
    require(spine.IsDone(), "Cannot build the sweep path.");

    auto sources = region;
    sources.push_back(featureKey);
    const auto key = topologyKey("sweep-region", std::move(sources));
    TopologyNames generated;
    const auto outer = BRepTools::OuterWire(contour->face);
    auto tool = sweepWire(spine.Wire(), outer, contour->boundaryNames, generated, key, true);
    TopTools_ListOfShape holes;
    for (TopExp_Explorer it(contour->face, TopAbs_WIRE); it.More(); it.Next())
        if (!outer.IsSame(it.Current()))
            holes.Append(sweepWire(spine.Wire(), TopoDS::Wire(it.Current()), contour->boundaryNames,
                                   generated, key, false));
    if (!holes.IsEmpty())
    {
        BRepAlgoAPI_Cut hollow;
        tool = combine(hollow, tool, holes, generated);
    }
    if (points.size() > 2)
    {
        BOPAlgo_ArgumentAnalyzer check;
        check.SetShape1(tool);
        check.SelfInterMode() = true;
        check.Perform();
        require(!check.HasFaulty(), "The sweep intersects itself. Reduce the profile size or change the path.");
    }
    if (base.IsNull())
    {
        generated.complete(tool);
        names = std::move(generated);
        return tool;
    }
    auto resultNames = names;
    for (const auto& [name, shapes] : generated.images)
        for (const auto& shape : shapes)
            resultNames.add(name, shape);
    TopTools_ListOfShape tools;
    tools.Append(tool);
    BRepAlgoAPI_Cut subtract;
    BRepAlgoAPI_Fuse add;
    auto result = combine(cut ? static_cast<BRepAlgoAPI_BooleanOperation&>(subtract) : add,
                          base, tools, resultNames);
    const double baseVolume = volume(base);
    const double change = cut ? baseVolume - volume(result) : volume(result) - baseVolume;
    require(change > std::max(1e-15, baseVolume * 1e-10),
            cut ? "Swept Cut does not remove material. Check the profile and path."
                : "Swept Boss/Base does not add material. Check the profile and path.");
    resultNames.complete(result);
    names = std::move(resultNames);
    return result;
}
} // namespace thecad::modeling
