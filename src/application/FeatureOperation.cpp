#include "application/FeatureOperation.h"
#include <Standard_Failure.hxx>
#include <stdexcept>
namespace thecad::application
{
FeatureOperationResult FeatureOperation::preview(const document::PartDocument& source,
                                                 const document::Feature& feature,
                                                 const modeling::RegionReferences& regions,
                                                 bool editing, PreviewHighlight highlight)
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
    modeling::MaterialChanges changes;
    if (highlight == PreviewHighlight::EdgeTreatmentFaces && document::isEdgeFeature(feature.kind)) {
        // Highlight generation must not invalidate a successful treatment.
        try {
            changes = modeling::edgeTreatmentHighlights(before, candidate.edgeTreatmentFaces(id));
        } catch (const Standard_Failure&) {
        } catch (const std::runtime_error&) {
        }
    } else {
        changes = modeling::materialChanges(before, result);
    }
    return {std::move(candidate), std::move(id), std::move(changes), result};
}
} // namespace thecad::application
