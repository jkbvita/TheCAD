#include "application/FeatureOperation.h"
namespace thecad::application
{
FeatureOperationResult FeatureOperation::preview(const document::PartDocument& source,
                                                 const document::Feature& feature,
                                                 const modeling::RegionReferences& regions,
                                                 bool editing)
{
    auto candidate = source;
    std::string id;
    if (editing)
    {
        candidate.replaceFeature(feature);
        id = feature.id;
    }
    else if (document::isRevolveFeature(feature.kind) || document::isEdgeFeature(feature.kind) ||
             document::isPatternFeature(feature.kind) || document::isSweepFeature(feature.kind) || feature.extrusion)
        id = candidate.addSolidFeature(feature);
    else if (feature.kind == document::FeatureKind::Cut)
        id = candidate.addCutRegions(feature.sketchId, regions, feature.length, feature.throughAll,
                                     feature.direction);
    else
        id = candidate.addExtrudeRegions(feature.sketchId, regions, feature.length,
                                         feature.direction);
    const auto before = editing ? source.solidBefore(id) : source.shape();
    const auto result = candidate.solidResult(id);
    auto changes = modeling::materialChanges(before, result);
    return {std::move(candidate), std::move(id), std::move(changes), result};
}
} // namespace thecad::application
