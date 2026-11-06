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
    else if (document::isRevolveFeature(feature.kind) || document::isEdgeFeature(feature.kind))
        id = candidate.addSolidFeature(feature);
    else if (feature.kind == document::FeatureKind::Cut)
        id = candidate.addCutRegions(feature.sketchId, regions, feature.length, feature.throughAll,
                                     feature.direction);
    else
        id = candidate.addExtrudeRegions(feature.sketchId, regions, feature.length,
                                         feature.direction);
    const auto changes = modeling::materialChanges(source.shape(), candidate.shape());
    return {std::move(candidate), std::move(id), changes};
}
} // namespace thecad::application
