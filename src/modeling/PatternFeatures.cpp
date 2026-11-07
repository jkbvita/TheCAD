#include "modeling/PatternFeatures.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace thecad::modeling
{
namespace
{
struct NamedMaterial
{
    TopoDS_Shape shape;
    TopologyNames names;
};

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

double volume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}

bool hasSolids(const TopoDS_Shape& shape)
{
    return !shape.IsNull() && TopExp_Explorer(shape, TopAbs_SOLID).More();
}

void addFaceNames(TopologyNames& target, const TopologyNames* source, const std::string& role)
{
    if (!source)
        return;
    for (const auto& [name, shapes] : source->images)
        for (const auto& shape : shapes)
            if (shape.ShapeType() == TopAbs_FACE)
                target.add(topologyKey(role, {name}), shape);
}

NamedMaterial difference(const TopoDS_Shape& first, const TopoDS_Shape& second,
                         const TopologyNames* firstNames, const TopologyNames* secondNames)
{
    if (first.IsNull())
        return {};
    TopologyNames names;
    addFaceNames(names, firstNames, "seed-material");
    if (second.IsNull())
        return {first, std::move(names)};
    addFaceNames(names, secondNames, "seed-support");
    TopTools_ListOfShape arguments, tools;
    arguments.Append(first);
    tools.Append(second);
    BRepAlgoAPI_Cut cut;
    cut.SetArguments(arguments);
    cut.SetTools(tools);
    cut.SetNonDestructive(true);
    cut.Build();
    require(cut.IsDone() && !cut.HasErrors(), "Cannot isolate the selected seed's material.");
    const auto result = cut.Shape();
    if (!hasSolids(result))
        return {};
    return {result, propagateTopology(names, cut, result)};
}

template <class Operation>
TopoDS_Shape applyMaterial(const TopoDS_Shape& base, const NamedMaterial& material,
                           const std::vector<gp_Trsf>& transforms, TopologyNames& names,
                           const std::string& instanceRole)
{
    TopTools_ListOfShape arguments, tools;
    arguments.Append(base);
    for (std::size_t i = 0; i < transforms.size(); ++i)
    {
        BRepBuilderAPI_Transform instance(material.shape, transforms[i], true);
        require(instance.IsDone(), "Cannot transform the seed geometry.");
        tools.Append(instance.Shape());
        const auto role = instanceRole + "-" + std::to_string(i + 1);
        for (const auto& [name, shapes] : material.names.images)
            for (const auto& shape : shapes)
                names.add(topologyKey(role, {name}), instance.ModifiedShape(shape));
    }
    Operation operation;
    operation.SetArguments(arguments);
    operation.SetTools(tools);
    operation.SetNonDestructive(true);
    operation.Build();
    require(operation.IsDone() && !operation.HasErrors(),
            "Cannot combine the transformed instances with the solid.");
    operation.SimplifyResult();
    const auto result = operation.Shape();
    require(hasSolids(result) && BRepCheck_Analyzer(result).IsValid(),
            "The instances must leave valid solid bodies.");
    names = propagateTopology(names, operation, result);
    return result;
}
}

std::vector<gp_Trsf> linearInstances(const gp_Dir& axis, double spacing, int count, bool reverse)
{
    require(std::isfinite(spacing) && spacing > 0 && spacing <= 1e9,
            "Pattern spacing must be positive and no larger than 1e9 mm.");
    require(count >= 2 && count <= maximumPatternInstances,
            "Enter between 2 and 100 instances, including the seed.");
    require(spacing * (count - 1) <= 1e9, "The last instance is too far from the seed.");
    const gp_Vec step = gp_Vec(axis) * (reverse ? -spacing : spacing);
    std::vector<gp_Trsf> result;
    for (int i = 1; i < count; ++i)
    {
        gp_Trsf translation;
        translation.SetTranslation(step * i);
        result.push_back(translation);
    }
    return result;
}

gp_Trsf mirrorInstance(const gp_Ax3& plane)
{
    gp_Trsf reflection;
    reflection.SetMirror(plane.Ax2());
    return reflection;
}

TopoDS_Shape applyPattern(const TopoDS_Shape& base, const std::vector<PatternSeed>& seeds,
                          const std::vector<gp_Trsf>& transforms, TopologyNames& names,
                          const std::string& featureId)
{
    require(hasSolids(base), "Pattern and Mirror require an existing solid.");
    require(!seeds.empty() && !transforms.empty(), "Select at least one seed feature or the solid bodies.");
    require(seeds.size() * transforms.size() <= maximumPatternInstances,
            "Select fewer seeds or instances. Up to 100 copies can be created in one feature.");
    auto body = base;
    double previousVolume = volume(base);
    bool changed = false;
    for (const auto& seed : seeds)
    {
        const auto added = difference(seed.result, seed.previous, seed.resultNames, seed.previousNames);
        const auto removed = difference(seed.previous, seed.result, seed.previousNames, seed.resultNames);
        require(!added.shape.IsNull() || !removed.shape.IsNull(),
                "A selected seed feature does not change solid material.");
        const auto apply = [&](auto operation, const NamedMaterial& material, const char* action) {
            if (material.shape.IsNull())
                return;
            const auto role = featureId + "-" + seed.id + "-" + action;
            body = applyMaterial<decltype(operation)>(body, material, transforms, names, role);
            const double currentVolume = volume(body);
            changed |= std::abs(currentVolume - previousVolume) >
                       std::max(1e-15, std::abs(previousVolume) * 1e-12);
            previousVolume = currentVolume;
        };
        apply(BRepAlgoAPI_Fuse{}, added, "add");
        apply(BRepAlgoAPI_Cut{}, removed, "remove");
    }
    require(changed, "The instances do not change the solid. Check the references and placement.");
    names.complete(body);
    return body;
}
}
