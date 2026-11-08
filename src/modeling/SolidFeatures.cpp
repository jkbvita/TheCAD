#include "modeling/SolidFeatures.h"
#include "modeling/PlanarEdgeTreatment.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <exception>
#include <Standard_Failure.hxx>
namespace thecad::modeling
{
namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void validSolid(const TopoDS_Shape& shape)
{
    require(!shape.IsNull() && TopExp_Explorer(shape, TopAbs_SOLID).More() &&
                BRepCheck_Analyzer(shape).IsValid(),
            "The feature must produce valid solid bodies. Reduce the parameter or change the "
            "selection.");
}
double volume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}
void mergeNames(TopologyNames& target, const TopologyNames& source)
{
    for (const auto& [key, shapes] : source.images)
        for (const auto& shape : shapes)
            target.add(key, shape);
}
template <class Operation>
void nameTreatment(TopologyNames& names, Operation& operation, const TopoDS_Shape& result,
                   const std::vector<TopoDS_Shape>& edges, const std::string& id)
{
    TopologyNames generated;
    for (const auto& edge : edges)
    {
        const auto source = names.nameOf(edge);
        require(!source.empty(),
                "An edge has no unique persistent name. Reselect a supported edge.");
        for (TopTools_ListIteratorOfListOfShape it(operation.Generated(edge)); it.More(); it.Next())
            if (it.Value().ShapeType() == TopAbs_FACE)
                generated.add(topologyKey("treatment", {id, source}), it.Value());
    }
    names = propagateTopology(names, operation, result);
    mergeNames(names, generated);
    names.complete(result);
}
} // namespace
TopoDS_Shape makeRevolutions(const TopoDS_Shape& base, const Sketch& sketch,
                             const RegionReferences& references, const gp_Ax1& axis,
                             double angleDegrees, Direction direction, bool cut,
                             TopologyNames& names, const std::string& featureId)
{
    require(std::isfinite(angleDegrees) && angleDegrees > 0 && angleDegrees <= 360,
            "Revolve angle must be greater than zero and at most 360 degrees.");
    require(!references.empty(), "Select at least one closed region.");
    require(!cut || !base.IsNull(), "Revolved Cut requires an existing solid.");
    const auto frame = sketchAxes(sketch);
    const gp_Vec normal(frame.Direction()), axisDirection(axis.Direction());
    require(std::abs(normal.Dot(axisDirection)) < 1e-9 &&
                std::abs(gp_Vec(frame.Location(), axis.Location()).Dot(normal)) < 1e-7,
            "The revolution axis must lie in the sketch plane.");
    gp_Trsf toAxis;
    toAxis.SetTransformation(gp_Ax3(axis.Location(), frame.Direction(), axis.Direction()));
    const auto contours = closedContours(sketch);
    std::set<std::set<std::string>> selected;
    TopologyNames generated;
    std::vector<TopoDS_Shape> revolutions;
    for (const auto& ids : references)
    {
        const std::set<std::string> key(ids.begin(), ids.end());
        require(!key.empty() && key.size() == ids.size() && selected.insert(key).second,
                "Selected regions contain empty or duplicate references.");
        const auto found =
            std::find_if(contours.begin(), contours.end(),
                         [&](const auto& region)
                         {
                             return std::set<std::string>(region.entityIds.begin(),
                                                          region.entityIds.end()) == key;
                         });
        require(found != contours.end(),
                "A selected region changed or disappeared. Reselect it explicitly.");
        Bnd_Box bounds;
        BRepBndLib::AddOptimal(BRepBuilderAPI_Transform(found->face, toAxis, true).Shape(), bounds,
                               false, false);
        double x0, y0, z0, x1, y1, z1;
        bounds.Get(x0, y0, z0, x1, y1, z1);
        require(!(y0 < -1e-7 && y1 > 1e-7), "A revolved profile cannot cross its axis.");
        const double angle = angleDegrees * 3.14159265358979323846 / 180 *
                             (direction == Direction::Positive ? 1 : -1);
        BRepPrimAPI_MakeRevol operation(found->face, axis, angle, true);
        require(operation.IsDone(), "Cannot revolve the selected region.");
        validSolid(operation.Shape());
        auto regionIds = ids;
        regionIds.push_back(featureId);
        const auto region = topologyKey("revolve-region", regionIds);
        if (!operation.FirstShape().IsNull() && operation.FirstShape().ShapeType() == TopAbs_FACE)
            generated.add(topologyKey("revolve-start", {region}), operation.FirstShape());
        if (!operation.LastShape().IsNull() && operation.LastShape().ShapeType() == TopAbs_FACE)
            generated.add(topologyKey("revolve-end", {region}), operation.LastShape());
        for (const auto& [boundary, edges] : found->boundaryNames.images)
            for (const auto& edge : edges)
                for (TopTools_ListIteratorOfListOfShape it(operation.Generated(edge)); it.More();
                     it.Next())
                    if (it.Value().ShapeType() == TopAbs_FACE)
                        generated.add(topologyKey("revolve-side", {featureId, boundary}),
                                      it.Value());
        revolutions.push_back(operation.Shape());
    }
    TopTools_ListOfShape arguments, tools;
    if (!base.IsNull())
    {
        BRepBuilderAPI_Copy copy(base, true, false);
        arguments.Append(copy.Shape());
        names = propagateTopology(names, copy, copy.Shape());
    }
    else
        arguments.Append(revolutions.front());
    for (std::size_t i = base.IsNull() ? 1 : 0; i < revolutions.size(); ++i)
        tools.Append(revolutions[i]);
    mergeNames(names, generated);
    if (tools.IsEmpty())
    {
        names.complete(revolutions.front());
        return revolutions.front();
    }
    const auto combine = [&](auto& operation)
    {
        operation.SetArguments(arguments);
        operation.SetTools(tools);
        operation.SetNonDestructive(true);
        operation.Build();
        require(operation.IsDone() && !operation.HasErrors(),
                "Cannot combine the revolved material with the part.");
        const auto result = operation.Shape();
        validSolid(result);
        if (cut)
            require(
                volume(base) - volume(result) > std::max(volume(base) * 1e-10, 1e-15),
                "Revolved Cut does not remove material. Check the profile, angle and direction.");
        names = propagateTopology(names, operation, result);
        names.complete(result);
        return result;
    };
    if (cut)
    {
        BRepAlgoAPI_Cut operation;
        return combine(operation);
    }
    BRepAlgoAPI_Fuse operation;
    return combine(operation);
}
TopoDS_Shape makeEdgeTreatment(const TopoDS_Shape& base, const std::vector<TopoDS_Shape>& edges,
                               double size, bool chamfer, TopologyNames& names,
                               const std::string& featureId)
{
    require(!base.IsNull(), "Fillet and Chamfer require an existing solid.");
    require(std::isfinite(size) && size > 0 && size <= 1e9,
            "Radius or distance must be positive and at most 1e9 mm.");
    require(!edges.empty() && edges.size() <= 1000, "Select between 1 and 1000 solid edges.");
    BRepBuilderAPI_Copy copy(base, true, false);
    auto privateNames = propagateTopology(names, copy, copy.Shape());
    TopTools_IndexedMapOfShape original;
    TopExp::MapShapes(base, TopAbs_EDGE, original);
    TopTools_IndexedMapOfShape unique;
    std::vector<TopoDS_Shape> privateEdges;
    for (const auto& edge : edges)
    {
        require(
            !edge.IsNull() && edge.ShapeType() == TopAbs_EDGE && original.Contains(edge),
            "A selected edge is no longer present in the preceding solid. Reselect it explicitly.");
        require(!unique.Contains(edge), "The same edge cannot be selected twice.");
        unique.Add(edge);
        const auto modified = copy.ModifiedShape(edge);
        require(!modified.IsNull(), "Cannot copy the selected edge.");
        privateEdges.push_back(modified);
    }
    const auto build = [&](auto& operation)
    {
        for (const auto& edge : privateEdges)
            operation.Add(size, TopoDS::Edge(edge));
        for (const auto& edge : privateEdges)
            require(operation.Contour(TopoDS::Edge(edge)) != 0,
                    "A selected edge is tangent or cannot take this treatment.");
        operation.Build();
        require(operation.IsDone(),
                "Cannot build the edge treatment. Reduce the size or change the selected edges.");
        validSolid(operation.Shape());
        nameTreatment(privateNames, operation, operation.Shape(), privateEdges, featureId);
        names = std::move(privateNames);
        return operation.Shape();
    };
    std::exception_ptr nativeFailure;
    try {
        if (chamfer) {
            BRepFilletAPI_MakeChamfer operation(copy.Shape());
            return build(operation);
        }
        BRepFilletAPI_MakeFillet operation(copy.Shape());
        return build(operation);
    } catch (const Standard_Failure&) {
        nativeFailure = std::current_exception();
    } catch (const std::runtime_error&) {
        nativeFailure = std::current_exception();
    }
    // Native rolling blends can fail where a straight planar edge terminates
    // at a mirror or at a previous blend. Keep the exact requested section.
    try {
        BRepBuilderAPI_Copy fresh(base, true, false);
        auto fallbackNames = propagateTopology(names, fresh, fresh.Shape());
        std::vector<TopoDS_Shape> fallbackEdges;
        for (const auto& edge : edges) fallbackEdges.push_back(fresh.ModifiedShape(edge));
        const auto fallback = cutPlanarEdgeTreatments(fresh.Shape(), fallbackEdges, size, chamfer,
                                                       fallbackNames, featureId);
        if (!fallback.IsNull()) {
            names = std::move(fallbackNames);
            return fallback;
        }
    } catch (const Standard_Failure&) {
        // Report the original modeling error if the exact fallback also fails.
    }
    std::rethrow_exception(nativeFailure);
}
} // namespace thecad::modeling
